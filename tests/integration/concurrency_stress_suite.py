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

"""

import os
import threading
import time
from pathlib import Path
from typing import List, Optional

import requests

from tests.support.api_helpers import assert_http_available
from tests.support.runtime_fixture import RuntimeFixture


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
        self.base_url = f"http://127.0.0.1:{http_port}"
        self.http_clients: List[ConcurrentHTTPClient] = []
        self.config_dict: Optional[dict] = None
        self.fixture: Optional[RuntimeFixture] = None
        self.runtime_path = runtime_path
        self.provider_path = provider_path

    def setup(self) -> None:
        """Validate paths and prepare test environment."""
        assert self.runtime_path.exists(), f"Runtime not found: {self.runtime_path}"
        assert self.provider_path.exists(), f"Provider not found: {self.provider_path}"

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

    def start_runtime(self, config_dict: dict) -> None:
        """Start runtime process."""
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
        assert self.fixture.start(), "Failed to start runtime via RuntimeFixture"
        assert assert_http_available(self.base_url, timeout=15.0), "Runtime not responsive after startup"

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

    def run_stress_test(self, num_restarts: int = 100) -> None:
        """Run the main stress test loop."""
        print(f"\n[STRESS TEST] {num_restarts} restarts with {self.num_clients} concurrent HTTP clients")

        # Setup
        config_dict = self.create_config()

        try:
            # Start runtime
            self.start_runtime(config_dict)

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
                    raise AssertionError(f"Runtime crashed during stress test at restart {i}")

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
                assert response.status_code < 500, "Runtime unresponsive after stress test"
            except Exception as e:
                raise AssertionError(f"Runtime unresponsive after stress test: {e}") from e

        finally:
            # Cleanup
            self.stop_http_clients()
            self.stop_runtime()
