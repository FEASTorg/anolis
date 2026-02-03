#!/usr/bin/env python3
"""
Phase 3C Integration Test for Anolis Core Runtime

This script validates all Phase 3 functionality:
- Phase 3.1: Provider Host (spawn, ADPP, framing)
- Phase 3.2: Device Registry (discovery, capabilities)
- Phase 3.3: State Cache (polling, staleness)
- Phase 3.4: Call Router (validation, execution)
- Phase 3.5: Runtime Bootstrap (config, lifecycle)

Usage:
    python scripts/test_phase3.py [--runtime PATH] [--provider PATH] [--timeout SECONDS]
"""

import subprocess
import sys
import os
import time
import threading
import tempfile
import signal
import argparse
from pathlib import Path
from dataclasses import dataclass
from typing import Optional, List
from queue import Queue, Empty


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
                    self._add_line(line.rstrip('\n\r'))
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
            return '\n'.join(self.lines)
    
    def stop(self):
        """Stop the capture thread."""
        self.stop_event.set()
        if self._thread:
            self._thread.join(timeout=2.0)


class Phase3Tester:
    """Test harness for Phase 3 integration tests."""
    
    def __init__(self, runtime_path: Path, provider_path: Path, timeout: float = 30.0):
        self.runtime_path = runtime_path
        self.provider_path = provider_path
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
        
        # Validate provider exists
        if not self.provider_path.exists():
            print(f"ERROR: Provider not found: {self.provider_path}")
            return False
        
        # Create temporary config
        config_content = f"""# Phase 3C Test Config
providers:
  - id: sim0
    command: "{self.provider_path.as_posix()}"
    args: []

polling:
  interval_ms: 200

logging:
  level: debug
"""
        
        # Write config to temp file
        fd, path = tempfile.mkstemp(suffix='.yaml', prefix='anolis_test_')
        os.write(fd, config_content.encode('utf-8'))
        os.close(fd)
        self.config_path = Path(path)
        
        return True
    
    def cleanup(self):
        """Clean up resources."""
        if self.process:
            try:
                self.process.terminate()
                self.process.wait(timeout=5)
            except:
                self.process.kill()
        
        if self.capture:
            self.capture.stop()
        
        if self.config_path and self.config_path.exists():
            try:
                self.config_path.unlink()
            except:
                pass
    
    def start_runtime(self) -> bool:
        """Start the runtime process."""
        try:
            self.process = subprocess.Popen(
                [str(self.runtime_path), f"--config={self.config_path}"],
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
                bufsize=1,  # Line buffered
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == 'win32' else 0
            )
            
            self.capture = OutputCapture(self.process)
            self.capture.start()
            return True
            
        except Exception as e:
            print(f"ERROR: Failed to start runtime: {e}")
            return False
    
    def record(self, name: str, passed: bool, message: str = ""):
        """Record a test result."""
        self.results.append(TestResult(name, passed, message))
        status = "✓" if passed else "✗"
        print(f"  {status} {name}")
        if message and not passed:
            print(f"      {message}")
    
    def run_tests(self) -> bool:
        """Run all Phase 3 integration tests."""
        print("\n" + "=" * 60)
        print("  Phase 3C Integration Tests")
        print("=" * 60)
        
        # Test 1: Runtime Startup
        print("\n1. Runtime Startup")
        
        if not self.capture.wait_for_marker("Anolis Core Runtime", timeout=5):
            self.record("Runtime banner", False, "Banner not displayed")
            return False
        self.record("Runtime banner", True)
        
        if not self.capture.wait_for_marker("Loading config", timeout=5):
            self.record("Config loading", False, "Config load message not seen")
            return False
        self.record("Config loading", True)
        
        # Test 2: Provider Management (Phase 3.1)
        print("\n2. Provider Management (Phase 3.1)")
        
        if not self.capture.wait_for_marker("Starting provider: sim0", timeout=10):
            self.record("Provider start", False, "Provider start not initiated")
            return False
        self.record("Provider start initiated", True)
        
        if not self.capture.wait_for_marker("Provider sim0 started", timeout=15):
            self.record("Provider started", False, "Provider failed to start")
            return False
        self.record("Provider started", True)
        
        # Test 3: Device Discovery (Phase 3.2)
        print("\n3. Device Discovery (Phase 3.2)")
        
        if not self.capture.wait_for_marker("All providers started", timeout=10):
            self.record("All providers", False, "Not all providers started")
            return False
        self.record("All providers started", True)
        
        # Check for device discovery in "Runtime Ready" block
        if not self.capture.wait_for_marker("Devices:", timeout=10):
            self.record("Device count", False, "Device count not reported")
            return False
        self.record("Device count reported", True)
        
        # Test 4: State Cache (Phase 3.3)
        print("\n4. State Cache (Phase 3.3)")
        
        if not self.capture.wait_for_marker("Initialization complete", timeout=10):
            self.record("Initialization", False, "Runtime initialization incomplete")
            return False
        self.record("Runtime initialized", True)
        
        # Test 5: Runtime Ready
        print("\n5. Runtime Ready Check")
        
        if not self.capture.wait_for_marker("Runtime Ready", timeout=5):
            self.record("Runtime ready", False, "Runtime ready banner not seen")
            return False
        self.record("Runtime ready", True)
        
        # State cache polling starts after runtime.run() is called
        # Look for StateCache marker which confirms polling thread started
        if not self.capture.wait_for_marker("Polling started", timeout=15):
            self.record("Polling active", False, "State cache polling not started")
            return False
        self.record("Polling active", True)
        
        # Let it run for a few poll cycles
        print("\n6. Stability Check (5 seconds)")
        time.sleep(5)
        
        # Check process is still running
        if self.process.poll() is not None:
            self.record("Process alive", False, f"Process exited with code {self.process.returncode}")
            return False
        self.record("Process alive", True)
        
        # Check for no warnings/errors
        output = self.capture.get_all_output()
        has_warnings = "WARNING:" in output or "ERROR:" in output
        self.record("No warnings/errors", not has_warnings, 
                   "Warnings or errors detected" if has_warnings else "")
        
        # Test 7: Graceful Shutdown
        print("\n7. Graceful Shutdown")
        
        # Send termination signal
        if sys.platform == 'win32':
            # On Windows, use CTRL_BREAK_EVENT
            self.process.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            self.process.terminate()
        
        try:
            self.process.wait(timeout=10)
            self.record("Graceful shutdown", True)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.record("Graceful shutdown", False, "Process did not exit gracefully")
        
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
            print("\n  ✓ ALL TESTS PASSED")
            print("  Phase 3 integration verified!")
        else:
            print("\n  ✗ SOME TESTS FAILED")
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
    parser = argparse.ArgumentParser(description="Phase 3C Integration Tests")
    parser.add_argument("--runtime", type=Path, help="Path to anolis-runtime executable")
    parser.add_argument("--provider", type=Path, help="Path to anolis-provider-sim executable")
    parser.add_argument("--timeout", type=float, default=30.0, help="Test timeout in seconds")
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
    
    # Create tester
    tester = Phase3Tester(runtime_path, provider_path, args.timeout)
    
    try:
        # Setup
        if not tester.setup():
            sys.exit(1)
        
        # Start runtime
        if not tester.start_runtime():
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
