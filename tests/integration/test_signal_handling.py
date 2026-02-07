#!/usr/bin/env python3
"""
Signal Handling Integration Test

Validates that the runtime gracefully handles SIGINT and SIGTERM signals
using async-signal-safe atomic flag pattern.

Tests:
1. SIGINT triggers clean shutdown (Ctrl+C simulation)
2. SIGTERM triggers clean shutdown (graceful termination)
3. No hangs or crashes during signal handling
4. Shutdown completes within reasonable timeout

Usage:
    python tests/integration/test_signal_handling.py [--runtime PATH] [--provider PATH]
"""

import argparse
import os
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

    def get_all_output(self) -> str:
        """Get all captured output as a single string."""
        with self.lock:
            return "\n".join(self.lines)

    def stop(self):
        """Stop the capture thread."""
        self.stop_event.set()
        if self._thread:
            self._thread.join(timeout=2.0)


def find_executable(name: str, hints: list[str]) -> Optional[str]:
    """Find executable by name, checking hints first."""
    # Check hints
    for hint in hints:
        if not hint:
            continue
        path = Path(hint)
        if path.exists() and path.is_file():
            return str(path)

    # Check PATH
    for path_dir in os.environ.get("PATH", "").split(os.pathsep):
        candidate = Path(path_dir) / name
        if candidate.exists():
            return str(candidate)

    return None


def test_signal_handling(
    runtime_path: str, provider_path: str, test_signal: signal.Signals, signal_name: str
) -> TestResult:
    """
    Test that runtime responds to signal and shuts down cleanly.

    Returns:
        TestResult with success/failure and message
    """
    # Create minimal config
    fd, config_path = tempfile.mkstemp(suffix=".yaml", prefix="anolis_signal_test_")
    try:
        config_content = f"""runtime:
  mode: MANUAL

http:
  enabled: true
  bind: "127.0.0.1"
  port: 8765

providers:
  - id: sim
    command: {provider_path}
    args: []

polling:
  interval_ms: 1000

logging:
  level: info

automation:
  enabled: false

telemetry:
  enabled: false
"""
        os.write(fd, config_content.encode("utf-8"))
        os.close(fd)

        # Start runtime
        proc = subprocess.Popen(
            [runtime_path, f"--config={config_path}"],
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
            creationflags=(subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0),
        )

        capture = OutputCapture(proc)
        capture.start()

        try:
            # Wait for runtime to become ready
            if not capture.wait_for_marker("Runtime Ready", timeout=15):
                proc.kill()
                proc.wait(timeout=2)
                return TestResult(signal_name, False, "Runtime did not become ready within 15s")

            # Wait a bit for stability
            time.sleep(0.5)

            # Send signal
            if sys.platform == "win32":
                # Windows: use CTRL_BREAK_EVENT which our handler can catch
                # CTRL_C_EVENT is problematic in subprocesses
                if test_signal == signal.SIGINT:
                    # SIGINT -> use CTRL_BREAK as proxy (closest to Unix SIGINT)
                    proc.send_signal(signal.CTRL_BREAK_EVENT)
                else:
                    # SIGTERM -> use CTRL_BREAK (Windows doesn't have real SIGTERM for processes)
                    proc.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                # Unix: send actual signal
                proc.send_signal(test_signal)

            # Wait for clean shutdown
            try:
                exit_code = proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=2)
                capture.stop()
                return TestResult(signal_name, False, "Runtime hung after signal (timeout after 5s)")

            capture.stop()

            # Verify graceful shutdown
            output = capture.get_all_output()

            # Check for shutdown message
            has_shutdown_msg = "Signal received" in output or "stopping" in output.lower() or "Shutting down" in output

            # On Windows, CTRL_BREAK might cause exit code 0 or non-zero
            # What matters is that it shut down without hanging
            if has_shutdown_msg or exit_code == 0:
                return TestResult(signal_name, True, f"Clean shutdown (exit: {exit_code})")
            else:
                # Even without explicit message, if it exited cleanly, that's okay
                if exit_code in [0, 255]:  # 255 can be -1 in unsigned
                    return TestResult(signal_name, True, f"Shutdown completed (exit: {exit_code})")
                return TestResult(signal_name, False, f"Unexpected exit code: {exit_code}")

        except Exception as e:
            proc.kill()
            proc.wait(timeout=2)
            capture.stop()
            return TestResult(signal_name, False, f"Exception: {e}")

    finally:
        try:
            os.unlink(config_path)
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(description="Test runtime signal handling")
    parser.add_argument("--runtime", help="Path to anolis-runtime executable")
    parser.add_argument("--provider", help="Path to provider executable")
    parser.add_argument("--timeout", type=float, default=60.0, help="Test timeout in seconds")
    args = parser.parse_args()

    # Find executables
    runtime_hints = [
        args.runtime if args.runtime else "",
        "build/core/Release/anolis-runtime.exe",
        "build/core/Debug/anolis-runtime.exe",
        "build/core/anolis-runtime",
    ]

    provider_hints = [
        args.provider if args.provider else "",
        "../anolis-provider-sim/build/Release/anolis-provider-sim.exe",
        "../anolis-provider-sim/build/Debug/anolis-provider-sim.exe",
        "../anolis-provider-sim/build/anolis-provider-sim",
    ]

    runtime_path = find_executable("anolis-runtime", runtime_hints)
    provider_path = find_executable("anolis-provider-sim", provider_hints)

    if not runtime_path:
        print("ERROR: Could not find anolis-runtime executable")
        print("  Tried:", [h for h in runtime_hints if h])
        return 1

    if not provider_path:
        print("ERROR: Could not find anolis-provider-sim executable")
        print("  Tried:", [h for h in provider_hints if h])
        return 1

    print("=" * 60)
    print("  Signal Handling Integration Tests")
    print("=" * 60)
    print(f"\nUsing runtime: {runtime_path}")
    print(f"Using provider: {provider_path}\n")

    # Run tests
    tests = []

    if sys.platform == "win32":
        # On Windows, both SIGINT and SIGTERM map to CTRL_BREAK_EVENT
        tests = [
            (signal.SIGINT, "SIGINT (Ctrl+C)"),
            (signal.SIGTERM, "SIGTERM (graceful)"),
        ]
    else:
        # On Unix, test actual signals
        tests = [
            (signal.SIGINT, "SIGINT (Ctrl+C)"),
            (signal.SIGTERM, "SIGTERM (kill)"),
        ]

    results = []
    for sig, description in tests:
        print(f"Testing {description}...")
        result = test_signal_handling(runtime_path, provider_path, sig, description)
        results.append(result)
        status = "[PASS]" if result.passed else "[FAIL]"
        print(f"  {status} {result.name}")
        if result.message:
            print(f"      {result.message}")
        print()

    # Summary
    passed = sum(1 for r in results if r.passed)
    total = len(results)

    print("=" * 60)
    print(f"  Test Summary: {passed}/{total} passed")
    print("=" * 60)

    if passed == total:
        print("\n  [PASS] ALL TESTS PASSED")
        print("  Signal handling validation verified!")
        return 0
    else:
        print("\n  [FAIL] SOME TESTS FAILED")
        print("\n  Failed tests:")
        for r in results:
            if not r.passed:
                print(f"    - {r.name}: {r.message}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
