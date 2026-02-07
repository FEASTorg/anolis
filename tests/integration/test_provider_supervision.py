#!/usr/bin/env python3
"""
Provider Supervision Integration Test

Validates automatic provider crash recovery with exponential backoff and circuit breaker.

Tests:
1. Provider crashes and restarts automatically
2. Exponential backoff delays are correct
3. Circuit breaker opens after max attempts
4. Devices rediscovered after successful restart
5. Supervisor state resets on recovery

Usage:
    python tests/integration/test_provider_supervision.py [--runtime PATH] [--crashable-provider PATH]
"""

import argparse
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from queue import Empty, Queue
from typing import List, Optional


@dataclass
class TestResult:
    name: str
    passed: bool
    message: str = ""


class OutputCapture:
    """Thread-safe output capture with timeout support."""

    def __init__(self, process: subprocess.Popen):
        self.process = process
        self.lines: List[str] = []
        self.lock = threading.Lock()
        self.queue: Queue = Queue()
        self.stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self):
        """Start capturing output in background thread."""
        self._thread = threading.Thread(target=self._capture_loop, daemon=True)
        self._thread.start()

    def _capture_loop(self):
        """Background thread that reads stderr."""
        try:
            while not self.stop_event.is_set():
                if self.process.poll() is not None:
                    # Process ended, read remaining output
                    remaining = self.process.stderr.read()
                    if remaining:
                        for line in remaining.splitlines():
                            self._add_line(line)
                    break

                line = self.process.stderr.readline()
                if line:
                    self._add_line(line.rstrip("\n\r"))
        except Exception as e:
            self._add_line(f"[CAPTURE ERROR] {e}")

    def _add_line(self, line: str):
        """Add line to buffer and queue."""
        with self.lock:
            self.lines.append(line)
        self.queue.put(line)

    def wait_for_marker(self, marker: str, timeout: float = 10.0) -> bool:
        """Wait for a specific marker to appear in output."""
        deadline = time.time() + timeout

        # First check existing lines
        with self.lock:
            for line in self.lines:
                if marker in line:
                    return True

        # Wait for new lines
        while time.time() < deadline:
            try:
                remaining = deadline - time.time()
                if remaining <= 0:
                    break
                line = self.queue.get(timeout=min(remaining, 0.5))
                if marker in line:
                    return True
            except Empty:
                continue

        return False

    def wait_for_pattern(self, pattern: str, timeout: float = 10.0) -> Optional[re.Match]:
        """Wait for a regex pattern to appear in output."""
        deadline = time.time() + timeout
        regex = re.compile(pattern)

        # First check existing lines
        with self.lock:
            for line in self.lines:
                match = regex.search(line)
                if match:
                    return match

        # Wait for new lines
        while time.time() < deadline:
            try:
                remaining = deadline - time.time()
                if remaining <= 0:
                    break
                line = self.queue.get(timeout=min(remaining, 0.5))
                match = regex.search(line)
                if match:
                    return match
            except Empty:
                continue

        return None

    def get_all_output(self) -> str:
        """Get all captured output as a single string."""
        with self.lock:
            return "\n".join(self.lines)

    def stop(self):
        """Stop the capture thread."""
        self.stop_event.set()
        if self._thread:
            self._thread.join(timeout=2.0)


class SupervisionTester:
    """Test harness for provider supervision integration tests."""

    def __init__(self, runtime_path: Path, crashable_provider_path: Path, timeout: float = 60.0):
        self.runtime_path = runtime_path
        self.crashable_provider_path = crashable_provider_path
        self.timeout = timeout
        self.process: Optional[subprocess.Popen] = None
        self.capture: Optional[OutputCapture] = None
        self.results: List[TestResult] = []
        self.config_path: Optional[Path] = None

    def setup(self) -> bool:
        """Create test config and validate paths."""
        # Validate runtime exists
        if not self.runtime_path.exists():
            print(f"ERROR: Runtime not found: {self.runtime_path}")
            return False

        # Validate crashable provider exists
        if not self.crashable_provider_path.exists():
            print(f"ERROR: Crashable provider not found: {self.crashable_provider_path}")
            return False

        return True

    def create_config(self, crash_after: float, max_attempts: int = 3, backoff_ms: List[int] = None) -> Path:
        """Create temporary config with supervision settings."""
        if backoff_ms is None:
            backoff_ms = [100, 1000, 5000]

        # Get python executable (convert to forward slashes for YAML)
        python_exe = sys.executable.replace("\\", "/")
        provider_path = str(self.crashable_provider_path).replace("\\", "/")

        config_content = f"""# Provider Supervision Test Config
providers:
  - id: crashable
    command: "{python_exe}"
    args: ["{provider_path}", "--crash-after", "{crash_after}"]
    timeout_ms: 5000
    restart_policy:
      enabled: true
      max_attempts: {max_attempts}
      backoff_ms: [{', '.join(map(str, backoff_ms))}]
      timeout_ms: 30000

polling:
  interval_ms: 200

logging:
  level: info
"""

        # Create temp file
        fd, path = tempfile.mkstemp(suffix=".yaml", text=True)
        with os.fdopen(fd, "w") as f:
            f.write(config_content)

        return Path(path)

    def start_runtime(self, config_path: Path) -> bool:
        """Start runtime with given config."""
        # Debug: print config content
        print(f"  Config file: {config_path}")
        with open(config_path) as f:
            print(f"  Config content:\n{f.read()}")
        
        cmd = [str(self.runtime_path), "--config", str(config_path)]
        print(f"  Command: {' '.join(cmd)}")
        
        try:
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                stdin=subprocess.DEVNULL,
                text=True,
                bufsize=1
            )
        except Exception as e:
            print(f"ERROR: Failed to start runtime: {e}")
            return False

        self.capture = OutputCapture(self.process)
        self.capture.start()

        # Wait for runtime to start
        if not self.capture.wait_for_marker("Runtime initialized", timeout=10.0):
            print("ERROR: Runtime failed to initialize")
            print(f"  Output:\n{self.capture.get_all_output()}")
            return False

        return True

    def stop_runtime(self):
        """Stop runtime gracefully."""
        if self.process:
            try:
                if sys.platform == "win32":
                    self.process.send_signal(signal.CTRL_C_EVENT)
                else:
                    self.process.send_signal(signal.SIGINT)
                
                self.process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            except Exception as e:
                print(f"WARNING: Error stopping runtime: {e}")
                self.process.kill()

        if self.capture:
            self.capture.stop()

    def test_automatic_restart(self) -> TestResult:
        """Test that provider restarts automatically after crash."""
        print("\n[TEST] Automatic Restart")
        
        # Create config with 2s crash, 3 attempts, short backoffs
        config_path = self.create_config(
            crash_after=2.0,
            max_attempts=3,
            backoff_ms=[200, 500, 1000]
        )

        try:
            # Start runtime
            if not self.start_runtime(config_path):
                return TestResult("automatic_restart", False, "Failed to start runtime")

            # Wait for initial provider start
            if not self.capture.wait_for_marker("Provider 'crashable' registered", timeout=10.0):
                return TestResult("automatic_restart", False, "Provider not registered")

            # Wait for first crash (after 2s)
            if not self.capture.wait_for_marker("crashed (attempt 1/3", timeout=5.0):
                output = self.capture.get_all_output()
                return TestResult("automatic_restart", False, f"No crash detected. Output:\n{output}")

            # Wait for restart attempt
            if not self.capture.wait_for_marker("Attempting to restart provider: crashable", timeout=2.0):
                return TestResult("automatic_restart", False, "No restart attempt logged")

            # Wait for successful restart
            if not self.capture.wait_for_marker("Provider restarted successfully", timeout=5.0):
                return TestResult("automatic_restart", False, "Provider failed to restart")

            # Wait for recovery message
            if not self.capture.wait_for_marker("recovered successfully", timeout=2.0):
                return TestResult("automatic_restart", False, "No recovery message")

            return TestResult("automatic_restart", True, "Provider restarted automatically after crash")

        finally:
            self.stop_runtime()
            config_path.unlink()

    def test_backoff_timing(self) -> TestResult:
        """Test that exponential backoff delays are correct."""
        print("\n[TEST] Backoff Timing")
        
        # Create config with 1s crash, backoffs: 500ms, 1000ms, 2000ms
        config_path = self.create_config(
            crash_after=1.0,
            max_attempts=3,
            backoff_ms=[500, 1000, 2000]
        )

        try:
            if not self.start_runtime(config_path):
                return TestResult("backoff_timing", False, "Failed to start runtime")

            # Wait for registration
            if not self.capture.wait_for_marker("Provider 'crashable' registered", timeout=10.0):
                return TestResult("backoff_timing", False, "Provider not registered")

            # Measure first crash and restart
            crash1_time = time.time()
            if not self.capture.wait_for_marker("crashed (attempt 1/3, retry in 500ms)", timeout=3.0):
                return TestResult("backoff_timing", False, "First crash not logged")

            restart1_time = time.time()
            if not self.capture.wait_for_marker("Attempting to restart", timeout=2.0):
                return TestResult("backoff_timing", False, "First restart not attempted")

            # Check first backoff (~500ms, allow 300-800ms tolerance)
            first_backoff = (restart1_time - crash1_time) * 1000
            if not (300 <= first_backoff <= 800):
                return TestResult("backoff_timing", False, 
                                f"First backoff incorrect: {first_backoff:.0f}ms (expected ~500ms)")

            # Wait for second crash (after ~1s provider uptime + restart time)
            crash2_time = time.time()
            if not self.capture.wait_for_marker("crashed (attempt 2/3, retry in 1000ms)", timeout=3.0):
                return TestResult("backoff_timing", False, "Second crash not logged")

            restart2_time = time.time()
            if not self.capture.wait_for_marker("Attempting to restart", timeout=3.0):
                return TestResult("backoff_timing", False, "Second restart not attempted")

            # Check second backoff (~1000ms, allow 800-1400ms tolerance)
            second_backoff = (restart2_time - crash2_time) * 1000
            if not (800 <= second_backoff <= 1400):
                return TestResult("backoff_timing", False,
                                f"Second backoff incorrect: {second_backoff:.0f}ms (expected ~1000ms)")

            return TestResult("backoff_timing", True, 
                            f"Backoff timing correct: {first_backoff:.0f}ms, {second_backoff:.0f}ms")

        finally:
            self.stop_runtime()
            config_path.unlink()

    def test_circuit_breaker(self) -> TestResult:
        """Test that circuit breaker opens after max attempts."""
        print("\n[TEST] Circuit Breaker")
        
        # Create config with 1s crash, only 2 attempts, short backoffs
        config_path = self.create_config(
            crash_after=1.0,
            max_attempts=2,
            backoff_ms=[200, 500]
        )

        try:
            if not self.start_runtime(config_path):
                return TestResult("circuit_breaker", False, "Failed to start runtime")

            # Wait for registration
            if not self.capture.wait_for_marker("Provider 'crashable' registered", timeout=10.0):
                return TestResult("circuit_breaker", False, "Provider not registered")

            # Wait for first crash
            if not self.capture.wait_for_marker("crashed (attempt 1/2", timeout=3.0):
                return TestResult("circuit_breaker", False, "First crash not detected")

            # Wait for second crash
            if not self.capture.wait_for_marker("crashed (attempt 2/2", timeout=3.0):
                return TestResult("circuit_breaker", False, "Second crash not detected")

            # Wait for third crash that should trigger circuit breaker
            if not self.capture.wait_for_marker("circuit breaker open", timeout=3.0):
                output = self.capture.get_all_output()
                return TestResult("circuit_breaker", False, 
                                f"Circuit breaker did not open. Output:\n{output}")

            # Verify no more restart attempts
            time.sleep(2.0)
            if "Attempting to restart" in self.capture.get_all_output().split("circuit breaker open")[-1]:
                return TestResult("circuit_breaker", False, 
                                "Restart attempted after circuit breaker opened")

            return TestResult("circuit_breaker", True, "Circuit breaker opened after max attempts")

        finally:
            self.stop_runtime()
            config_path.unlink()

    def test_device_rediscovery(self) -> TestResult:
        """Test that devices are rediscovered after provider restart."""
        print("\n[TEST] Device Rediscovery")
        
        # Create config with 2s crash
        config_path = self.create_config(
            crash_after=2.0,
            max_attempts=3,
            backoff_ms=[200, 500, 1000]
        )

        try:
            if not self.start_runtime(config_path):
                return TestResult("device_rediscovery", False, "Failed to start runtime")

            # Wait for initial device discovery
            if not self.capture.wait_for_marker("Registered: crashable/crash_device", timeout=10.0):
                return TestResult("device_rediscovery", False, "Initial device not discovered")

            # Wait for crash
            if not self.capture.wait_for_marker("crashed (attempt 1/3", timeout=5.0):
                return TestResult("device_rediscovery", False, "No crash detected")

            # Wait for device clearing
            if not self.capture.wait_for_marker("Clearing devices for provider: crashable", timeout=2.0):
                return TestResult("device_rediscovery", False, "Devices not cleared before restart")

            # Wait for restart
            if not self.capture.wait_for_marker("Provider restarted successfully", timeout=5.0):
                return TestResult("device_rediscovery", False, "Provider failed to restart")

            # Wait for device rediscovery
            if not self.capture.wait_for_marker("Registered: crashable/crash_device", timeout=2.0):
                return TestResult("device_rediscovery", False, "Device not rediscovered after restart")

            return TestResult("device_rediscovery", True, "Devices rediscovered after successful restart")

        finally:
            self.stop_runtime()
            config_path.unlink()

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
                    print(f"  ✅ PASS: {result.message}")
                else:
                    print(f"  ❌ FAIL: {result.message}")
            except Exception as e:
                print(f"  ❌ EXCEPTION: {e}")
                self.results.append(TestResult(test_fn.__name__, False, f"Exception: {e}"))

        # Print summary
        print("\n" + "="*70)
        print("SUMMARY")
        print("="*70)
        
        passed = sum(1 for r in self.results if r.passed)
        total = len(self.results)
        
        for result in self.results:
            status = "✅ PASS" if result.passed else "❌ FAIL"
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
            for pattern in [name, f"{name}.exe", f"Release/{name}.exe", f"Debug/{name}.exe"]:
                candidate = search_path / pattern
                if candidate.exists():
                    return candidate
    return None


def main():
    parser = argparse.ArgumentParser(description="Provider Supervision Integration Test")
    parser.add_argument("--runtime", type=Path, help="Path to anolis runtime executable")
    parser.add_argument("--crashable-provider", type=Path, help="Path to crashable_provider.py")
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

    # Find crashable provider
    if args.crashable_provider:
        crashable_path = args.crashable_provider
    else:
        crashable_path = Path(__file__).parent / "crashable_provider.py"

    if not crashable_path.exists():
        print(f"ERROR: Crashable provider not found: {crashable_path}")
        return 1

    print("="*70)
    print("PROVIDER SUPERVISION INTEGRATION TEST")
    print("="*70)
    print(f"Runtime: {runtime_path}")
    print(f"Crashable Provider: {crashable_path}")
    print(f"Timeout: {args.timeout}s")

    tester = SupervisionTester(runtime_path, crashable_path, args.timeout)
    return tester.run_tests()


if __name__ == "__main__":
    sys.exit(main())
