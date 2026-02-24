#!/usr/bin/env python3
"""
Provider Supervision Integration Test

Validates automatic provider crash recovery with exponential backoff and circuit breaker.
Uses anolis-provider-sim with --crash-after flag for chaos testing.

Tests:
1. Provider crashes and restarts automatically
2. Exponential backoff delays are correct
3. Circuit breaker opens after max attempts
4. Devices rediscovered after successful restart
5. Supervisor state resets on recovery

Usage:
    python tests/integration/test_provider_supervision.py [--runtime PATH] [--provider-sim PATH]
"""

import argparse
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

from test_fixtures import RuntimeFixture
from test_helpers import (
    assert_http_available,
    assert_provider_available,
    get_provider_health_entry,
)


@dataclass
class TestResult:
    name: str
    passed: bool
    message: str = ""


class SupervisionTester:
    """Test harness for provider supervision integration tests."""

    def __init__(self, runtime_path: Path, provider_sim_path: Path, timeout: float = 60.0):
        self.runtime_path = runtime_path
        self.provider_sim_path = provider_sim_path
        self.timeout = timeout
        self.results: List[TestResult] = []
        self.fixture: Optional[RuntimeFixture] = None
        self.base_url = "http://localhost:8080"

    def setup(self) -> bool:
        """Create test config and validate paths."""
        # Validate runtime exists
        if not self.runtime_path.exists():
            print(f"ERROR: Runtime not found: {self.runtime_path}")
            return False

        # Validate provider-sim exists
        if not self.provider_sim_path.exists():
            print(f"ERROR: Provider-sim not found: {self.provider_sim_path}")
            return False

        return True

    @property
    def capture(self):
        """Get OutputCapture from fixture for convenience."""
        if self.fixture:
            return self.fixture.get_output_capture()
        return None

    def create_config(self, crash_after: float, max_attempts: int = 3, backoff_ms: Optional[List[int]] = None) -> dict:
        """Create config dict with supervision settings."""
        if backoff_ms is None:
            backoff_ms = [100, 1000, 5000]

        provider_cmd = str(self.provider_sim_path).replace("\\", "/")
        fixture_config = Path(__file__).parent / "fixtures" / "provider-sim-default.yaml"

        config = {
            "http": {
                "enabled": True,
                "bind": "127.0.0.1",
                "port": 8080,
            },
            "providers": [
                {
                    "id": "provider-sim",
                    "command": provider_cmd,
                    "args": ["--config", str(fixture_config).replace("\\", "/"), "--crash-after", str(crash_after)],
                    "timeout_ms": 1500,
                    # Keep supervision tests responsive: lower provider RPC timeout reduces
                    # crash-detection latency and avoids flaky timing races in CI.
                    "restart_policy": {
                        "enabled": True,
                        "max_attempts": max_attempts,
                        "backoff_ms": backoff_ms,
                        "timeout_ms": 30000,
                    },
                }
            ],
            "polling": {"interval_ms": 200},
            "logging": {"level": "info"},
        }

        return config

    def start_runtime(self, config_dict: dict) -> bool:
        """Start runtime with given config."""
        print("  Starting runtime with RuntimeFixture")

        try:
            self.fixture = RuntimeFixture(
                self.runtime_path,
                self.provider_sim_path,
                config_dict=config_dict,
            )

            if not self.fixture.start():
                print("ERROR: Failed to start runtime")
                return False

            # Use fixture-provided base URL (avoids hard-coded host/port assumptions).
            self.base_url = self.fixture.base_url

        except Exception as e:
            print(f"ERROR: Failed to start runtime: {e}")
            return False

        # Wait for HTTP server to respond — API-based readiness check.
        if not assert_http_available(self.base_url, timeout=10.0):
            print("ERROR: Runtime HTTP server did not become available")
            capture = self.fixture.get_output_capture()
            if capture:
                print(f"  Output:\n{capture.get_all_output()}")
            return False

        return True

    def stop_runtime(self):
        """Stop runtime gracefully."""
        if self.fixture:
            try:
                self.fixture.cleanup()
            except Exception:
                pass  # Cleanup error, ignore

    def _sample_provider_health(self) -> dict:
        """Capture a compact provider health snapshot for debug timelines."""
        ts = time.time()
        entry = get_provider_health_entry(self.base_url, "provider-sim", timeout=1.0)
        if entry is None:
            return {
                "t": ts,
                "present": False,
                "state": "MISSING",
                "attempt_count": None,
                "crash_detected": None,
                "circuit_open": None,
                "max_attempts": None,
                "next_restart_in_ms": None,
                "uptime_seconds": None,
                "device_count": None,
            }

        supervision = entry.get("supervision") or {}
        return {
            "t": ts,
            "present": True,
            "state": entry.get("state"),
            "attempt_count": supervision.get("attempt_count"),
            "crash_detected": supervision.get("crash_detected"),
            "circuit_open": supervision.get("circuit_open"),
            "max_attempts": supervision.get("max_attempts"),
            "next_restart_in_ms": supervision.get("next_restart_in_ms"),
            "uptime_seconds": entry.get("uptime_seconds"),
            "device_count": entry.get("device_count"),
        }

    def _wait_for_snapshot_predicate(self, predicate, timeout: float, interval: float = 0.1):
        """
        Poll provider health until predicate(snapshot) is true.
        Returns (matched: bool, snapshots: list[dict]).
        """
        snapshots = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            snap = self._sample_provider_health()
            snapshots.append(snap)
            if predicate(snap):
                return True, snapshots
            time.sleep(interval)
        return False, snapshots

    def _format_health_snapshots(self, snapshots: List[dict], tail: int = 40) -> str:
        """Format compact health timeline for failure diagnostics."""
        if not snapshots:
            return "(no health snapshots captured)"
        start_t = snapshots[0]["t"]
        lines = []
        for snap in snapshots[-tail:]:
            dt_ms = int((snap["t"] - start_t) * 1000)
            lines.append(
                f"+{dt_ms:5d}ms "
                f"present={snap['present']} "
                f"state={snap['state']} "
                f"attempt={snap['attempt_count']} "
                f"crash={snap['crash_detected']} "
                f"circuit={snap['circuit_open']} "
                f"max={snap['max_attempts']} "
                f"next={snap['next_restart_in_ms']} "
                f"uptime={snap['uptime_seconds']} "
                f"devices={snap['device_count']}"
            )
        return "\n".join(lines)

    def _fail_with_output(
        self,
        name: str,
        message: str,
        tail_lines: int = 200,
        health_snapshots: Optional[List[dict]] = None,
    ) -> TestResult:
        """Build a failure result with recent runtime output context."""
        extra = ""
        if health_snapshots:
            extra = "\nHealth timeline tail:\n" + self._format_health_snapshots(health_snapshots)

        if self.capture is None:
            return TestResult(name, False, message + extra)

        output_tail = self.capture.get_recent_output(tail_lines)
        return TestResult(name, False, f"{message}{extra}\nOutput tail:\n{output_tail}")

    def test_automatic_restart(self) -> TestResult:
        """Test that provider restarts automatically after crash."""
        print("\n[TEST] Automatic Restart")

        config_dict = self.create_config(crash_after=2.0, max_attempts=3, backoff_ms=[200, 500, 1000])

        try:
            if not self.start_runtime(config_dict):
                return TestResult("automatic_restart", False, "Failed to start runtime")

            # Wait for provider to be initially available.
            if not assert_provider_available(self.base_url, "provider-sim", timeout=10.0):
                return self._fail_with_output("automatic_restart", "Provider not available at startup")

            # Wait for the provider chaos crash banner (provider stderr — acceptable trigger,
            # not a pass/fail oracle; just advances timing certainty).
            if self.capture:
                self.capture.wait_for_marker("CRASHING NOW (exit 42)", timeout=8.0)

            # API oracle: provider must go down (UNAVAILABLE or temporarily missing from health list during restart).
            went_down, down_snaps = self._wait_for_snapshot_predicate(
                lambda s: (not s["present"]) or s["state"] == "UNAVAILABLE",
                timeout=8.0,
                interval=0.05,
            )
            if not went_down:
                return self._fail_with_output(
                    "automatic_restart",
                    "Provider never observed down after crash",
                    health_snapshots=down_snaps,
                )

            # API oracle: provider recovered — AVAILABLE and attempt_count reset to 0.
            recovered, rec_snaps = self._wait_for_snapshot_predicate(
                lambda s: s["present"] and s["state"] == "AVAILABLE" and s["attempt_count"] == 0,
                timeout=10.0,
                interval=0.05,
            )
            if not recovered:
                return self._fail_with_output(
                    "automatic_restart",
                    "Provider did not recover",
                    health_snapshots=rec_snaps,
                )

            return TestResult("automatic_restart", True, "Provider restarted automatically after crash")

        finally:
            self.stop_runtime()

    def test_backoff_timing(self) -> TestResult:
        """Test that exponential backoff delays are correct.

        Validates:
        - max_attempts configuration is correctly exposed via API
        - Wall-clock time from UNAVAILABLE→AVAILABLE transition is consistent with
          the configured first backoff (500 ms ± generous tolerance for CI)
        """
        print("\n[TEST] Backoff Timing")

        config_dict = self.create_config(crash_after=1.0, max_attempts=3, backoff_ms=[500, 1000, 2000])

        try:
            if not self.start_runtime(config_dict):
                return TestResult("backoff_timing", False, "Failed to start runtime")

            # Wait for provider to be initially available.
            if not assert_provider_available(self.base_url, "provider-sim", timeout=10.0):
                return self._fail_with_output("backoff_timing", "Provider not available at startup")

            # Use provider crash banner as timing trigger only (provider stderr, not runtime oracle).
            if self.capture:
                self.capture.wait_for_marker("CRASHING NOW (exit 42)", timeout=6.0)

            # API oracle: confirm max_attempts reflects configured policy.
            entry = get_provider_health_entry(self.base_url, "provider-sim")
            if entry is None:
                return self._fail_with_output("backoff_timing", "Could not query provider health")

            actual_max = entry["supervision"]["max_attempts"]
            if actual_max != 3:
                return self._fail_with_output(
                    "backoff_timing",
                    f"Unexpected max_attempts in supervision: {actual_max} (expected 3)",
                )

            # Measure wall-clock between UNAVAILABLE and next AVAILABLE transition.
            # This captures: detection-lag + backoff-wait + process-restart-time.
            went_down, down_snaps = self._wait_for_snapshot_predicate(
                lambda s: (not s["present"]) or s["state"] == "UNAVAILABLE",
                timeout=8.0,
                interval=0.05,
            )
            if not went_down:
                return self._fail_with_output(
                    "backoff_timing",
                    "Provider did not go down after crash",
                    health_snapshots=down_snaps,
                )
            t_unavailable = time.time()

            recovered, rec_snaps = self._wait_for_snapshot_predicate(
                lambda s: s["present"] and s["state"] == "AVAILABLE",
                timeout=10.0,
                interval=0.05,
            )
            if not recovered:
                return self._fail_with_output(
                    "backoff_timing",
                    "Provider did not recover",
                    health_snapshots=rec_snaps,
                )
            t_available = time.time()

            elapsed_ms = (t_available - t_unavailable) * 1000

            # Lower bound: backoff must have elapsed (minus detection overhead).
            # Upper bound: generous — shared CI runners can be slow.
            if not (100 <= elapsed_ms <= 3500):
                return self._fail_with_output(
                    "backoff_timing",
                    f"UNAVAILABLE->AVAILABLE transition took {elapsed_ms:.0f}ms (expected 100-3500ms, ~500ms backoff)",
                )

            return TestResult("backoff_timing", True, f"Backoff timing correct: {elapsed_ms:.0f}ms")

        finally:
            self.stop_runtime()

    def test_circuit_breaker(self) -> TestResult:
        """Test that circuit breaker opens after max_attempts consecutive crashes.

        Uses crash_after=0.1s with poll_interval=200ms so the provider crashes before
        the first successful poll, preventing record_success from resetting attempt_count.
        After max_attempts+1 crashes the circuit breaker opens and no further restarts occur.
        """
        print("\n[TEST] Circuit Breaker")

        # crash_after=0.1s < poll_interval=0.2s: provider never polled successfully,
        # attempt_count accumulates until circuit opens.
        config_dict = self.create_config(crash_after=0.1, max_attempts=2, backoff_ms=[100, 200])

        try:
            if not self.start_runtime(config_dict):
                return TestResult("circuit_breaker", False, "Failed to start runtime")

            # API oracle: wait for circuit breaker to open.
            saw_crash, crash_snaps = self._wait_for_snapshot_predicate(
                lambda s: s["present"] and ((s["attempt_count"] or 0) >= 1 or s["crash_detected"] is True),
                timeout=12.0,
                interval=0.05,
            )
            if not saw_crash:
                return self._fail_with_output(
                    "circuit_breaker",
                    "No crash-attempt state was observed before circuit-open check",
                    health_snapshots=crash_snaps,
                )

            opened, circuit_snaps = self._wait_for_snapshot_predicate(
                lambda s: s["present"] and s["circuit_open"] is True,
                timeout=15.0,
                interval=0.05,
            )
            if not opened:
                return self._fail_with_output(
                    "circuit_breaker",
                    "Circuit breaker did not open",
                    health_snapshots=crash_snaps + circuit_snaps,
                )

            # Verify final supervision state.
            entry = get_provider_health_entry(self.base_url, "provider-sim")
            if entry is None:
                return self._fail_with_output("circuit_breaker", "Failed to query provider health after circuit open")

            if entry["state"] != "UNAVAILABLE":
                return self._fail_with_output(
                    "circuit_breaker",
                    f"Expected UNAVAILABLE after circuit open, got {entry['state']}",
                )

            attempt_count = entry["supervision"]["attempt_count"]
            if attempt_count < 3:  # max_attempts=2 → circuit opens on 3rd crash
                return self._fail_with_output(
                    "circuit_breaker",
                    f"Expected attempt_count >= 3 when circuit opens, got {attempt_count}",
                )

            return TestResult(
                "circuit_breaker",
                True,
                f"Circuit breaker opened after {attempt_count} attempts (next_restart_in_ms: "
                f"{entry['supervision']['next_restart_in_ms']})",
            )

        finally:
            self.stop_runtime()

    def test_device_rediscovery(self) -> TestResult:
        """Test that devices are rediscovered after provider restart."""
        print("\n[TEST] Device Rediscovery")

        config_dict = self.create_config(crash_after=2.0, max_attempts=3, backoff_ms=[200, 500, 1000])

        try:
            if not self.start_runtime(config_dict):
                return TestResult("device_rediscovery", False, "Failed to start runtime")

            # API oracle: wait for provider to be initially AVAILABLE with devices.
            if not assert_provider_available(self.base_url, "provider-sim", timeout=10.0):
                return self._fail_with_output("device_rediscovery", "Provider not available at startup")

            # Use provider crash banner as timing trigger only.
            if self.capture:
                self.capture.wait_for_marker("CRASHING NOW (exit 42)", timeout=8.0)

            # API oracle: provider goes down after crash (UNAVAILABLE or temporarily missing during restart).
            went_down, down_snaps = self._wait_for_snapshot_predicate(
                lambda s: (not s["present"]) or s["state"] == "UNAVAILABLE",
                timeout=8.0,
                interval=0.05,
            )
            if not went_down:
                return self._fail_with_output(
                    "device_rediscovery",
                    "Provider did not go down after crash",
                    health_snapshots=down_snaps,
                )

            # API oracle: provider recovers and is AVAILABLE again (devices rediscovered).
            recovered, rec_snaps = self._wait_for_snapshot_predicate(
                lambda s: s["present"] and s["state"] == "AVAILABLE" and s["attempt_count"] == 0,
                timeout=12.0,
                interval=0.05,
            )
            if not recovered:
                return self._fail_with_output(
                    "device_rediscovery",
                    "Provider did not recover after restart",
                    health_snapshots=rec_snaps,
                )

            # Confirm devices are present post-recovery.
            entry = get_provider_health_entry(self.base_url, "provider-sim")
            if entry is None or entry.get("device_count", 0) == 0:
                return self._fail_with_output("device_rediscovery", "No devices reported after recovery")

            return TestResult(
                "device_rediscovery",
                True,
                f"Devices rediscovered after restart (device_count={entry['device_count']})",
            )

        finally:
            self.stop_runtime()

    def run_tests(self) -> int:
        """Run all supervision tests."""
        if not self.setup():
            return 1

        # Run each test
        tests = [
            self.test_automatic_restart,
            self.test_backoff_timing,
            self.test_circuit_breaker,
            self.test_device_rediscovery,
        ]

        for test_fn in tests:
            try:
                result = test_fn()
                self.results.append(result)

                if result.passed:
                    print(f"  [PASS] {result.message}")
                else:
                    print(f"  [FAIL] {result.message}")
            except Exception as e:
                print(f"  [EXCEPTION] {e}")
                self.results.append(TestResult(test_fn.__name__, False, f"Exception: {e}"))

        # Print summary
        print("\n" + "=" * 70)
        print("SUMMARY")
        print("=" * 70)

        passed = sum(1 for r in self.results if r.passed)
        total = len(self.results)

        for result in self.results:
            status = "[PASS]" if result.passed else "[FAIL]"
            print(f"{status}: {result.name}")
            if not result.passed and result.message:
                print(f"       {result.message}")

        print(f"\nTotal: {passed}/{total} passed")

        return 0 if passed == total else 1


def find_executable(name: str, search_paths: List[Path]) -> Optional[Path]:
    """Find executable in search paths."""
    for search_path in search_paths:
        if search_path.exists():
            if search_path.is_file():
                return search_path
            # Search in directory
            for pattern in [
                name,
                f"{name}.exe",
                f"Release/{name}.exe",
                f"Debug/{name}.exe",
            ]:
                candidate = search_path / pattern
                if candidate.exists():
                    return candidate
    return None


def main():
    # -------------------------------------------------------------------------
    # ThreadSanitizer (TSAN) and Timing Test Detection
    # -------------------------------------------------------------------------
    # This test validates supervision timing contracts (backoff delays,
    # crash detection windows) that are tuned for production performance.
    # TSAN adds 5-20x slowdown to process operations that breaks these:
    #   - Provider processes take >1s to spawn (vs <50ms normally)
    #   - IPC timeouts trigger before handshakes complete
    #   - Backoff delays don't match wall-clock time due to TSAN overhead
    #
    # Skip conditions:
    #   - ANOLIS_SKIP_TIMING_TESTS=1: Explicit timing test skip flag
    #   - ANOLIS_TSAN=1 or TSAN_OPTIONS set: TSAN environment detected
    #
    # TSAN's purpose is race detection (validated by unit tests), not
    # timing precision. Functional correctness is verified without TSAN.
    # -------------------------------------------------------------------------
    skip_timing = os.environ.get("ANOLIS_SKIP_TIMING_TESTS") == "1"
    tsan_env = os.environ.get("ANOLIS_TSAN") == "1" or bool(os.environ.get("TSAN_OPTIONS"))

    if skip_timing or tsan_env:
        print("=" * 70)
        print("SKIPPING: Provider supervision test")
        print("=" * 70)
        if skip_timing:
            print("Reason: ANOLIS_SKIP_TIMING_TESTS=1")
        else:
            print("Reason: TSAN detected (TSAN_OPTIONS/ANOLIS_TSAN)")
            print("        TSAN adds 5-20x slowdown that breaks timing assumptions")
        print("")
        print("Race detection: Covered by unit tests under TSAN")
        print("Functional correctness: Validated without TSAN overhead")
        print("=" * 70)
        return 0

    parser = argparse.ArgumentParser(description="Provider Supervision Integration Test")
    parser.add_argument("--runtime", type=Path, help="Path to anolis runtime executable")
    parser.add_argument("--provider-sim", type=Path, help="Path to anolis-provider-sim executable")
    parser.add_argument("--provider", dest="provider_sim", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--timeout", type=float, default=60.0, help="Test timeout in seconds")
    args = parser.parse_args()

    # Find runtime
    if args.runtime:
        runtime_path = args.runtime
    else:
        search_paths = [
            Path("build/core/Release/anolis-runtime.exe"),
            Path("build/core/Debug/anolis-runtime.exe"),
            Path("build/anolis-runtime"),
            Path("../build/core/Release/anolis-runtime.exe"),
        ]
        runtime_path = find_executable("anolis-runtime", search_paths)

    if not runtime_path or not runtime_path.exists():
        print("ERROR: Runtime not found. Use --runtime to specify path")
        return 1

    # Find provider-sim
    if args.provider_sim:
        provider_sim_path = args.provider_sim
    else:
        search_paths = [
            Path("../anolis-provider-sim/build/Release/anolis-provider-sim.exe"),
            Path("../anolis-provider-sim/build/Debug/anolis-provider-sim.exe"),
            Path("../../anolis-provider-sim/build/Release/anolis-provider-sim.exe"),
        ]
        provider_sim_path = find_executable("anolis-provider-sim", search_paths)

    if not provider_sim_path or not provider_sim_path.exists():
        print("ERROR: Provider-sim not found. Use --provider-sim to specify path")
        return 1

    print("=" * 70)
    print("PROVIDER SUPERVISION INTEGRATION TEST")
    print("=" * 70)
    print(f"Runtime: {runtime_path}")
    print(f"Provider-Sim: {provider_sim_path}")
    print(f"Timeout: {args.timeout}s")

    tester = SupervisionTester(runtime_path, provider_sim_path, args.timeout)
    return tester.run_tests()


if __name__ == "__main__":
    sys.exit(main())
