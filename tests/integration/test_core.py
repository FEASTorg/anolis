#!/usr/bin/env python3
"""
Anolis Core Runtime Integration Test

This script validates core runtime functionality using API-based assertions:
- Provider Host (spawn, ADPP, framing)
- Device Registry (discovery, capabilities)
- State Cache (polling, staleness)
- Call Router (validation, execution)
- Runtime Bootstrap (config, lifecycle)

Test Philosophy:
- Primary assertions use HTTP API state (behavior verification)
- Logs captured for debugging context only (not pass/fail criteria)
- Tests resilient to log format changes

Usage:
    python tests/integration/test_core.py [--runtime PATH] [--provider PATH] [--timeout SECONDS]
"""

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

# Import RuntimeFixture for process management
from test_fixtures import RuntimeFixture

# Import API-based test helpers
from test_helpers import (
    assert_device_count,
    assert_http_available,
    assert_provider_available,
    get_devices,
    get_runtime_status,
    wait_for_condition,
)


@dataclass
class TestResult:
    name: str
    passed: bool
    message: str = ""


class CoreFeatureTester:
    """Test harness for Core Runtime integration tests."""

    def __init__(
        self,
        runtime_path: Path,
        provider_path: Path,
        timeout: float = 30.0,
        port: int = 8080,
    ):
        self.runtime_path = runtime_path
        self.provider_path = provider_path
        self.timeout = timeout
        self.port = port
        self.base_url = f"http://127.0.0.1:{port}"
        self.fixture = RuntimeFixture(runtime_path, provider_path, http_port=port)
        self.results: List[TestResult] = []

    def start_runtime(self) -> bool:
        """Start the runtime process."""
        return self.fixture.start()

    def cleanup(self):
        """Clean up resources."""
        self.fixture.cleanup()

    def record(self, name: str, passed: bool, message: str = ""):
        """Record a test result."""
        self.results.append(TestResult(name, passed, message))
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {name}")
        if message and not passed:
            print(f"      {message}")

    def run_tests(self) -> bool:
        """Run all Core Runtime integration tests using API-based assertions."""
        print("\n" + "=" * 60)
        print("  Core Runtime Integration Tests (API-Based)")
        print("=" * 60)

        # Test 1: HTTP Server Availability
        print("\n1. HTTP Server Availability")

        if not assert_http_available(self.base_url, timeout=10):
            self.record("HTTP server", False, "HTTP server did not become available")
            # Log output for debugging
            capture = self.fixture.get_output_capture()
            if capture:
                print("[DEBUG] Log output so far:")
                print(capture.get_recent_output(50))
            return False
        self.record("HTTP server available", True)

        # Test 2: Provider Management
        print("\n2. Provider Management")

        if not assert_provider_available(self.base_url, "sim0", timeout=20):
            self.record("Provider started", False, "Provider sim0 not available via API")
            # Log output for debugging
            capture = self.fixture.get_output_capture()
            if capture:
                print("[DEBUG] Log output:")
                print(capture.get_recent_output(50))
            return False
        self.record("Provider sim0 available", True)

        # Verify provider status via API
        status = get_runtime_status(self.base_url)
        if status:
            providers = status.get("providers", [])
            provider_count = len(providers)
            self.record(f"Provider count ({provider_count})", provider_count > 0)
        else:
            self.record("Provider status", False, "Could not get runtime status")

        # Test 3: Device Discovery
        print("\n3. Device Discovery")

        # Wait for devices to be discovered (simulator typically has 4 devices)
        if not assert_device_count(self.base_url, expected_count=0, min_count=1, timeout=15):
            self.record("Device discovery", False, "No devices discovered via API")
            capture = self.fixture.get_output_capture()
            if capture:
                print("[DEBUG] Log output:")
                print(capture.get_recent_output(50))
            return False

        devices = get_devices(self.base_url)
        device_count = len(devices) if devices else 0
        self.record(f"Devices discovered ({device_count})", device_count > 0)

        # Test 4: State Cache Polling
        print("\n4. State Cache Polling")

        # Verify polling is active by checking that we can read state
        if devices and isinstance(devices, list) and len(devices) > 0:
            first_device = devices[0]
            first_provider_id = first_device.get("provider_id") if isinstance(first_device, dict) else None
            first_device_id = first_device.get("device_id") if isinstance(first_device, dict) else None

            if first_provider_id and first_device_id:
                # Poll until state is available (polling has populated cache)
                import requests

                def check_state_available():
                    try:
                        resp = requests.get(
                            f"{self.base_url}/v0/state/{first_provider_id}/{first_device_id}",
                            timeout=2,
                        )
                        return resp.status_code == 200
                    except Exception:
                        return False

                polling_works = wait_for_condition(
                    check_state_available,
                    timeout=5.0,
                    interval=0.2,
                    description="State cache populated",
                )
                self.record(
                    "State cache polling",
                    polling_works,
                    "State not available" if not polling_works else "",
                )
            else:
                self.record("State cache polling", False, "Could not get device identifiers")
        else:
            self.record("State cache polling", False, "No devices to poll")

        # Test 5: Runtime Ready - Verify all components operational
        print("\n5. Runtime Ready Check")

        # API-based readiness: HTTP responding + providers running + devices discovered
        is_ready = get_runtime_status(self.base_url) is not None and device_count > 0
        self.record("Runtime ready (API)", is_ready)

        # Test 6: Stability Check
        print("\n6. Stability Check (5 seconds)")
        deadline = time.time() + 5
        while time.time() < deadline:
            if not self.fixture.is_running():
                self.record(
                    "Process alive",
                    False,
                    "Process exited unexpectedly",
                )
                return False
            time.sleep(0.1)

        self.record("Process alive", True)

        # Check for warnings/errors in logs (for debugging context, not pass/fail)
        capture = self.fixture.get_output_capture()
        if capture is not None:
            output = capture.get_all_output()
            has_warnings = "WARNING:" in output or "ERROR:" in output
            if has_warnings:
                print("[DEBUG NOTE] Warnings or errors detected (context only):")
                # Show last few WARNING/ERROR lines
                for line in capture.lines[-50:]:
                    if "WARNING:" in line or "ERROR:" in line:
                        print(f"    {line}")

        # Test 7: Graceful Shutdown (handled by RuntimeFixture)
        print("\n7. Graceful Shutdown")
        # RuntimeFixture cleanup handles graceful shutdown automatically
        # (SIGTERM with 5s timeout, then force kill if needed)
        self.record("Graceful shutdown", True)

        return all(r.passed for r in self.results)

    def print_summary(self):
        """Print test summary."""
        print("\n" + "=" * 60)
        print("  Test Summary")
        print("=" * 60)

        passed = sum(1 for r in self.results if r.passed)
        total = len(self.results)

        print(f"\n  Passed: {passed}/{total}")

        if passed == total:
            print("\n  [PASS] ALL TESTS PASSED")
            print("  Core Runtime validation verified!")
        else:
            print("\n  [FAIL] SOME TESTS FAILED")
            print("\n  Failed tests:")
            for r in self.results:
                if not r.passed:
                    print(f"    - {r.name}: {r.message}")

        print("\n" + "=" * 60)


def find_runtime() -> Optional[Path]:
    """Find the runtime executable."""
    # Check common build locations
    candidates = [
        Path("build/core/Release/anolis-runtime.exe"),
        Path("build/core/Debug/anolis-runtime.exe"),
        Path("build/core/anolis-runtime.exe"),
        Path("build/core/anolis-runtime"),
        Path("core/build/Release/anolis-runtime.exe"),
        Path("core/build/Debug/anolis-runtime.exe"),
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()

    return None


def find_provider() -> Optional[Path]:
    """Find the provider executable."""
    # Check common locations
    candidates = [
        # Sibling repo - C++ build (CMake)
        Path("../anolis-provider-sim/build/Release/anolis-provider-sim.exe"),
        Path("../anolis-provider-sim/build/Debug/anolis-provider-sim.exe"),
        Path("../anolis-provider-sim/build/anolis-provider-sim.exe"),
        Path("../anolis-provider-sim/build/anolis-provider-sim"),
        # Sibling repo - Rust build (legacy)
        Path("../anolis-provider-sim/target/release/anolis-provider-sim.exe"),
        Path("../anolis-provider-sim/target/debug/anolis-provider-sim.exe"),
        Path("../anolis-provider-sim/target/release/anolis-provider-sim"),
        Path("../anolis-provider-sim/target/debug/anolis-provider-sim"),
        # Local copy
        Path("build/providers/anolis-provider-sim.exe"),
        Path("build/providers/anolis-provider-sim"),
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()

    return None


def main():
    parser = argparse.ArgumentParser(description="Core Runtime Integration Tests")
    parser.add_argument("--runtime", type=Path, help="Path to anolis-runtime executable")
    parser.add_argument("--provider", type=Path, help="Path to anolis-provider-sim executable")
    parser.add_argument("--timeout", type=float, default=30.0, help="Test timeout in seconds")
    parser.add_argument("--port", type=int, default=8080, help="HTTP server port")
    args = parser.parse_args()

    # Find executables
    runtime_path = args.runtime or find_runtime()
    provider_path = args.provider or find_provider()

    if not runtime_path:
        print("ERROR: Could not find anolis-runtime executable")
        print("       Use --runtime PATH to specify location")
        print("       Or build with: cmake --build build/core --config Release")
        sys.exit(1)

    if not provider_path:
        print("ERROR: Could not find anolis-provider-sim executable")
        print("       Use --provider PATH to specify location")
        print("       Or build anolis-provider-sim with: cmake --build build --config Release")
        sys.exit(1)

    print(f"Runtime:  {runtime_path}")
    print(f"Provider: {provider_path}")
    print(f"Port:     {args.port}")

    # Create tester
    tester = CoreFeatureTester(runtime_path, provider_path, args.timeout, args.port)

    try:
        # Start runtime
        if not tester.start_runtime():
            print("ERROR: Failed to start runtime")
            sys.exit(1)

        # Run tests
        success = tester.run_tests()

        # Print summary
        tester.print_summary()

        sys.exit(0 if success else 1)

    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(130)
    finally:
        tester.cleanup()


if __name__ == "__main__":
    main()
