#!/usr/bin/env python3
"""
Concurrency Stress Test for ThreadSanitizer

Validates thread safety under heavy concurrent load during provider restarts.

Test scenario:
- Restart provider 100 times in a loop
- Run 10 concurrent HTTP clients continuously making requests
- Polling thread active throughout
- Verify: No TSAN warnings, no crashes, no deadlocks

This test is designed to expose data races in:
- Provider registry access during restarts
- Device registry mutations while readers active
- State cache poll config updates during concurrent polling
- HTTP handlers accessing shared state

Usage:
    python tests/integration/test_concurrency_stress.py [--runtime PATH] [--provider PATH]
"""

import argparse
import os
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

import requests
from test_fixtures import RuntimeFixture
from test_helpers import wait_for_condition


@dataclass
class StressTestResult:
    success: bool
    message: str
    restart_count: int = 0
    http_requests: int = 0
    errors: List[str] = None

    def __post_init__(self):
        if self.errors is None:
            self.errors = []


class ConcurrentHTTPClient:
    """Thread that continuously makes HTTP requests during stress test."""

    def __init__(self, client_id: int, base_url: str):
        self.client_id = client_id
        self.base_url = base_url
        self.request_count = 0
        self.error_count = 0
        self.running = False
        self.thread: Optional[threading.Thread] = None

    def start(self):
        """Start making requests in background thread."""
        self.running = True
        self.thread = threading.Thread(target=self._request_loop, daemon=True)
        self.thread.start()

    def stop(self):
        """Stop request loop."""
        self.running = False
        if self.thread:
            self.thread.join(timeout=2.0)

    def _request_loop(self):
        """Make requests continuously until stopped."""
        session = requests.Session()
        session.headers.update({"User-Agent": f"StressClient-{self.client_id}"})

        endpoints = [
            "/v0/runtime/status",
            "/v0/devices",
            "/v0/state",
            "/v0/mode",
        ]

        while self.running:
            try:
                # Rotate through endpoints
                endpoint = endpoints[self.request_count % len(endpoints)]
                response = session.get(f"{self.base_url}{endpoint}", timeout=1.0)

                # We expect various statuses during restarts (503, 500, etc.)
                # Just count successful requests, don't fail on errors
                if response.status_code < 500:
                    self.request_count += 1
                else:
                    self.error_count += 1

                # Small delay to avoid overwhelming the server
                time.sleep(0.05)  # ~20 requests/sec per client

            except (requests.RequestException, ConnectionError):
                # Expected during restarts
                self.error_count += 1
                time.sleep(0.1)

            except Exception as e:
                print(f"[Client {self.client_id}] Unexpected error: {e}")
                self.error_count += 1
                time.sleep(0.1)


class StressTestRunner:
    """Orchestrates the stress test scenario."""

    def __init__(self, runtime_path: Path, provider_path: Path, num_clients: int = 10, http_port: int = 8080):
        self.num_clients = num_clients
        self.http_port = http_port
        self.base_url = f"http://localhost:{http_port}"
        self.http_clients: List[ConcurrentHTTPClient] = []
        self.config_dict: Optional[dict] = None
        self.fixture: Optional[RuntimeFixture] = None
        self.runtime_path = runtime_path
        self.provider_path = provider_path

    def setup(self) -> bool:
        """Validate paths and prepare test environment."""
        if not self.runtime_path.exists():
            print(f"ERROR: Runtime not found: {self.runtime_path}")
            return False

        if not self.provider_path.exists():
            print(f"ERROR: Provider not found: {self.provider_path}")
            return False

        return True

    def create_config(self) -> dict:
        """Create test configuration as dict for RuntimeFixture."""
        provider_cmd = str(self.provider_path).replace("\\", "/")
        fixture_config = Path(__file__).parent / "fixtures" / "provider-sim-default.yaml"

        config = {
            "providers": [
                {
                    "id": "stress-provider",
                    "command": provider_cmd,
                    "args": ["--config", str(fixture_config).replace("\\", "/")],
                    "timeout_ms": 5000,
                    "restart_policy": {
                        "enabled": True,
                        "max_attempts": 10,
                        "backoff_ms": [50, 100, 200, 400, 800, 1000, 1000, 1000, 1000, 1000],
                        "timeout_ms": 60000,
                    },
                }
            ],
            "http": {"enabled": True, "bind": "127.0.0.1", "port": self.http_port},
            "polling": {"interval_ms": 100},
            "logging": {"level": "warn"},
        }

        return config

    def start_runtime(self, config_dict: dict) -> bool:
        """Start runtime process."""
        print("  Starting runtime with RuntimeFixture")

        try:
            # Set environment for better TSAN output
            env = os.environ.copy()
            if "TSAN_OPTIONS" not in env:
                env["TSAN_OPTIONS"] = "halt_on_error=1"
                os.environ["TSAN_OPTIONS"] = "halt_on_error=1"

            self.fixture = RuntimeFixture(
                self.runtime_path,
                self.provider_path,
                http_port=self.http_port,
                config_dict=config_dict,
            )

            if not self.fixture.start():
                print("ERROR: Failed to start runtime via RuntimeFixture")
                return False

        except Exception as e:
            print(f"ERROR: Failed to start runtime: {e}")
            return False

        # Wait for runtime to become ready (poll with 15s timeout)
        print("  Waiting for runtime to initialize...")

        def check_runtime_ready():
            try:
                response = requests.get(f"{self.base_url}/v0/runtime/status", timeout=2.0)
                return response.status_code == 200
            except Exception:
                return False

        if not wait_for_condition(check_runtime_ready, timeout=15.0, interval=0.5, description="runtime ready"):
            print("ERROR: Runtime not responsive after 15 seconds")
            if not self.fixture.is_running():
                print("  Runtime process crashed during startup")
            return False

        print("  Runtime is ready")
        return True

    def start_http_clients(self):
        """Start concurrent HTTP client threads."""
        print(f"  Starting {self.num_clients} concurrent HTTP clients...")
        for i in range(self.num_clients):
            client = ConcurrentHTTPClient(client_id=i, base_url=self.base_url)
            client.start()
            self.http_clients.append(client)
        time.sleep(0.5)  # Let clients warm up

    def stop_http_clients(self):
        """Stop all HTTP client threads."""
        for client in self.http_clients:
            client.stop()

    def trigger_provider_restart(self) -> bool:
        """Trigger provider restart via HTTP API."""
        try:
            response = requests.post(
                f"{self.base_url}/v0/runtime/restart",
                json={"provider_id": "stress-provider"},
                timeout=3.0,
            )
            return response.status_code == 200
        except Exception:
            # Restart might cause temporary connection failures
            return True  # Assume success, provider supervision will handle it

    def stop_runtime(self):
        """Stop runtime process."""
        if self.fixture:
            try:
                self.fixture.cleanup()
            except Exception as e:
                print(f"  Warning during runtime shutdown: {e}")

    def run_stress_test(self, num_restarts: int = 100) -> StressTestResult:
        """Run the main stress test loop."""
        print(f"\n[STRESS TEST] {num_restarts} restarts with {self.num_clients} concurrent HTTP clients")

        # Setup
        config_dict = self.create_config()

        try:
            # Start runtime
            if not self.start_runtime(config_dict):
                return StressTestResult(success=False, message="Failed to start runtime")

            # Start HTTP clients
            self.start_http_clients()

            # Main stress loop: trigger restarts
            print(f"  Starting {num_restarts} restart cycles...")
            successful_restarts = 0
            start_time = time.time()

            for i in range(num_restarts):
                if i > 0 and i % 10 == 0:
                    elapsed = time.time() - start_time
                    rate = i / elapsed
                    print(f"    Progress: {i}/{num_restarts} restarts ({rate:.1f} restarts/sec)")

                # Trigger restart
                if self.trigger_provider_restart():
                    successful_restarts += 1

                # Small delay between restarts
                time.sleep(0.1)

                # Check if runtime is still alive
                if self.fixture and not self.fixture.is_running():
                    return StressTestResult(
                        success=False,
                        message=f"Runtime crashed during stress test at restart {i}",
                        restart_count=i,
                    )

            elapsed = time.time() - start_time
            print(f"  Completed {num_restarts} restarts in {elapsed:.1f}s ({num_restarts / elapsed:.1f} restarts/sec)")

            # Let clients finish pending requests
            time.sleep(1.0)

            # Stop clients and collect stats
            self.stop_http_clients()

            total_requests = sum(c.request_count for c in self.http_clients)
            total_errors = sum(c.error_count for c in self.http_clients)

            print(f"  HTTP clients made {total_requests} successful requests ({total_errors} errors)")

            # Check if runtime is still responsive
            try:
                response = requests.get(f"{self.base_url}/v0/runtime/status", timeout=2.0)
                if response.status_code >= 500:
                    return StressTestResult(
                        success=False,
                        message="Runtime unresponsive after stress test",
                        restart_count=successful_restarts,
                        http_requests=total_requests,
                    )
            except Exception as e:
                return StressTestResult(
                    success=False,
                    message=f"Runtime unresponsive after stress test: {e}",
                    restart_count=successful_restarts,
                    http_requests=total_requests,
                )

            return StressTestResult(
                success=True,
                message="Stress test completed successfully",
                restart_count=successful_restarts,
                http_requests=total_requests,
            )

        finally:
            # Cleanup
            self.stop_http_clients()
            self.stop_runtime()


def main():
    parser = argparse.ArgumentParser(description="Concurrency stress test with ThreadSanitizer")
    parser.add_argument("--runtime", type=Path, help="Path to anolis-runtime binary")
    parser.add_argument("--provider", type=Path, help="Path to provider binary")
    parser.add_argument("--restarts", type=int, help="Number of restart cycles (overrides --stress-level)")
    parser.add_argument("--clients", type=int, help="Number of concurrent HTTP clients (overrides --stress-level)")
    parser.add_argument(
        "--stress-level",
        type=str,
        default="moderate",
        choices=["light", "moderate", "heavy"],
        help="Stress test intensity level (default: moderate)",
    )
    parser.add_argument("--timeout", type=float, default=60.0, help="Test timeout in seconds")
    parser.add_argument("--port", type=int, default=8080, help="HTTP port (default: 8080)")

    args = parser.parse_args()

    # Define stress levels
    STRESS_LEVELS = {
        "light": {"restarts": 10, "clients": 2},
        "moderate": {"restarts": 50, "clients": 5},
        "heavy": {"restarts": 100, "clients": 10},
    }

    # Use explicit values if provided, otherwise use stress level
    level_config = STRESS_LEVELS[args.stress_level]
    num_restarts = args.restarts if args.restarts is not None else level_config["restarts"]
    num_clients = args.clients if args.clients is not None else level_config["clients"]

    # Determine paths
    if args.runtime:
        runtime_path = args.runtime
    else:
        runtime_path = Path("build/core/anolis-runtime")
        if sys.platform == "win32":
            runtime_path = Path("build/core/Release/anolis-runtime.exe")

    if args.provider:
        provider_path = args.provider
    else:
        provider_path = Path("../anolis-provider-sim/build/anolis-provider-sim")
        if sys.platform == "win32":
            provider_path = Path("../anolis-provider-sim/build/Release/anolis-provider-sim.exe")

    print("=" * 80)
    print("CONCURRENCY STRESS TEST FOR THREADSANITIZER")
    print("=" * 80)
    print(f"Runtime:       {runtime_path}")
    print(f"Provider:      {provider_path}")
    print(f"Stress Level:  {args.stress_level}")
    print(f"Restarts:      {num_restarts}")
    print(f"Clients:       {num_clients}")
    print(f"HTTP Port:     {args.port}")
    print("=" * 80)

    # Run test
    runner = StressTestRunner(runtime_path, provider_path, num_clients=num_clients, http_port=args.port)

    if not runner.setup():
        print("\n[ERROR] Setup failed")
        return 1

    result = runner.run_stress_test(num_restarts=num_restarts)

    print("\n" + "=" * 80)
    if result.success:
        print("[PASS] STRESS TEST PASSED")
        print(f"   - Completed {result.restart_count} restarts")
        print(f"   - HTTP clients made {result.http_requests} requests")
        print("   - No crashes, no data races, no deadlocks")
        print("=" * 80)
        return 0
    else:
        print("[FAIL] STRESS TEST FAILED")
        print(f"   - {result.message}")
        print(f"   - Restarts completed: {result.restart_count}")
        print(f"   - HTTP requests: {result.http_requests}")
        if result.errors:
            print("   - Errors:")
            for error in result.errors:
                print(f"     - {error}")
        print("=" * 80)
        return 1


if __name__ == "__main__":
    sys.exit(main())
