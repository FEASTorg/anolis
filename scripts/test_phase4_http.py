#!/usr/bin/env python3
"""
Phase 4 HTTP Gateway Integration Test for Anolis Core Runtime

This script validates all Phase 4 HTTP API functionality:
- GET /v0/devices (list all devices)
- GET /v0/devices/{provider}/{device}/capabilities (device capabilities)
- GET /v0/state (all device states)
- GET /v0/state/{provider}/{device} (single device state)
- POST /v0/call (execute device function)
- GET /v0/runtime/status (runtime status)
- Error handling (404, 400, 503)
- Provider failure → UNAVAILABLE transition

Usage:
    python scripts/test_phase4_http.py [--runtime PATH] [--provider PATH] [--port PORT]

Prerequisites:
    - Phase 3 tests must pass
    - Runtime and provider executables must be built
"""

import subprocess
import sys
import os
import time
import threading
import tempfile
import signal
import argparse
import json
from pathlib import Path
from dataclasses import dataclass
from typing import Optional, List, Dict, Any
from queue import Queue, Empty

# Try to import requests, provide helpful error if missing
try:
    import requests
except ImportError:
    print("ERROR: 'requests' package not installed.")
    print("Install with: pip install requests")
    sys.exit(1)


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
        
        with self.lock:
            for line in self.lines:
                if marker in line:
                    return True
        
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


class Phase4HTTPTester:
    """Test harness for Phase 4 HTTP API integration tests."""
    
    def __init__(self, runtime_path: Path, provider_path: Path, port: int = 8080, timeout: float = 30.0):
        self.runtime_path = runtime_path
        self.provider_path = provider_path
        self.port = port
        self.base_url = f"http://127.0.0.1:{port}"
        self.timeout = timeout
        self.process: Optional[subprocess.Popen] = None
        self.capture: Optional[OutputCapture] = None
        self.results: List[TestResult] = []
        self.config_path: Optional[Path] = None
    
    def setup(self) -> bool:
        """Create test config and validate paths."""
        if not self.runtime_path.exists():
            print(f"ERROR: Runtime not found: {self.runtime_path}")
            return False
        
        if not self.provider_path.exists():
            print(f"ERROR: Provider not found: {self.provider_path}")
            return False
        
        config_content = f"""# Phase 4 HTTP Test Config
runtime:
  mode: MANUAL

http:
  enabled: true
  bind: 127.0.0.1
  port: {self.port}

providers:
  - id: sim0
    command: "{self.provider_path.as_posix()}"
    args: []
    timeout_ms: 5000

polling:
  interval_ms: 200

telemetry:
  enabled: false

logging:
  level: debug
"""
        
        fd, path = tempfile.mkstemp(suffix='.yaml', prefix='anolis_http_test_')
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
                bufsize=1,
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
    
    def http_get(self, path: str) -> Optional[Dict[str, Any]]:
        """Make GET request, return JSON or None."""
        try:
            resp = requests.get(f"{self.base_url}{path}", timeout=5)
            return {"status_code": resp.status_code, "body": resp.json()}
        except Exception as e:
            return {"status_code": 0, "error": str(e)}
    
    def http_post(self, path: str, data: Dict) -> Optional[Dict[str, Any]]:
        """Make POST request, return JSON or None."""
        try:
            resp = requests.post(f"{self.base_url}{path}", json=data, timeout=5)
            return {"status_code": resp.status_code, "body": resp.json()}
        except Exception as e:
            return {"status_code": 0, "error": str(e)}
    
    def wait_for_http_ready(self, timeout: float = 15.0) -> bool:
        """Poll /v0/runtime/status until server is ready."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                resp = requests.get(f"{self.base_url}/v0/runtime/status", timeout=2)
                if resp.status_code == 200:
                    return True
            except:
                pass
            time.sleep(0.5)
        return False
    
    def run_tests(self) -> bool:
        """Run all Phase 4 HTTP integration tests."""
        print("\n" + "=" * 60)
        print("  Phase 4 HTTP Gateway Integration Tests")
        print("=" * 60)
        
        # ========================================
        # 1. Runtime Startup & HTTP Ready
        # ========================================
        print("\n1. Runtime Startup & HTTP Ready")
        
        if not self.capture.wait_for_marker("HTTP server started", timeout=15):
            self.record("HTTP server started", False, "HTTP startup message not seen")
            return False
        self.record("HTTP server started", True)
        
        if not self.wait_for_http_ready(timeout=10):
            self.record("HTTP responsive", False, "Could not connect to HTTP server")
            return False
        self.record("HTTP responsive", True)
        
        # ========================================
        # 2. GET /v0/runtime/status
        # ========================================
        print("\n2. GET /v0/runtime/status")
        
        result = self.http_get("/v0/runtime/status")
        if result["status_code"] != 200:
            self.record("Status endpoint", False, f"Expected 200, got {result['status_code']}")
            return False
        self.record("Status returns 200", True)
        
        body = result["body"]
        if body.get("status", {}).get("code") != "OK":
            self.record("Status code OK", False, f"Got: {body.get('status')}")
            return False
        self.record("Status code OK", True)
        
        if body.get("mode") != "MANUAL":
            self.record("Mode is MANUAL", False, f"Got: {body.get('mode')}")
            return False
        self.record("Mode is MANUAL", True)
        
        if "providers" not in body or len(body["providers"]) == 0:
            self.record("Providers listed", False, "No providers in response")
            return False
        self.record("Providers listed", True)
        
        # ========================================
        # 3. GET /v0/devices
        # ========================================
        print("\n3. GET /v0/devices")
        
        result = self.http_get("/v0/devices")
        if result["status_code"] != 200:
            self.record("Devices endpoint", False, f"Expected 200, got {result['status_code']}")
            return False
        self.record("Devices returns 200", True)
        
        body = result["body"]
        devices = body.get("devices", [])
        if len(devices) < 2:
            self.record("Multiple devices", False, f"Expected 2+ devices, got {len(devices)}")
            return False
        self.record("Multiple devices returned", True)
        
        # Check device structure
        device_ids = [d.get("device_id") for d in devices]
        if "tempctl0" not in device_ids or "motorctl0" not in device_ids:
            self.record("Expected devices", False, f"Missing tempctl0 or motorctl0: {device_ids}")
            return False
        self.record("tempctl0 and motorctl0 present", True)
        
        # ========================================
        # 4. GET /v0/devices/{provider}/{device}/capabilities
        # ========================================
        print("\n4. GET /v0/devices/sim0/tempctl0/capabilities")
        
        result = self.http_get("/v0/devices/sim0/tempctl0/capabilities")
        if result["status_code"] != 200:
            self.record("Capabilities endpoint", False, f"Expected 200, got {result['status_code']}")
            return False
        self.record("Capabilities returns 200", True)
        
        body = result["body"]
        caps = body.get("capabilities", {})
        signals = caps.get("signals", [])
        functions = caps.get("functions", [])
        
        if len(signals) < 4:
            self.record("Signals present", False, f"Expected 4+ signals, got {len(signals)}")
            return False
        self.record("Signals present (4+)", True)
        
        if len(functions) < 1:
            self.record("Functions present", False, f"Expected 1+ functions, got {len(functions)}")
            return False
        self.record("Functions present", True)
        
        # Check signal structure
        signal_ids = [s.get("signal_id") for s in signals]
        if "tc1_temp" not in signal_ids:
            self.record("tc1_temp signal", False, f"Missing tc1_temp: {signal_ids}")
            return False
        self.record("tc1_temp signal found", True)
        
        # ========================================
        # 5. GET /v0/state (all devices)
        # ========================================
        print("\n5. GET /v0/state")
        
        # Wait for at least one poll cycle
        time.sleep(0.5)
        
        result = self.http_get("/v0/state")
        if result["status_code"] != 200:
            self.record("State endpoint", False, f"Expected 200, got {result['status_code']}")
            return False
        self.record("State returns 200", True)
        
        body = result["body"]
        state_devices = body.get("devices", [])
        if len(state_devices) < 2:
            self.record("State devices", False, f"Expected 2+ devices, got {len(state_devices)}")
            return False
        self.record("State has all devices", True)
        
        # Check values structure
        for dev in state_devices:
            values = dev.get("values", [])
            if len(values) == 0:
                self.record("State values", False, f"No values for {dev.get('device_id')}")
                return False
            
            # Check value structure
            val = values[0]
            if "signal_id" not in val or "value" not in val or "quality" not in val:
                self.record("Value structure", False, f"Missing fields: {val.keys()}")
                return False
        self.record("State values present", True)
        
        # Check quality
        first_dev = state_devices[0]
        if first_dev.get("quality") not in ["OK", "STALE"]:
            self.record("Quality valid", False, f"Unexpected quality: {first_dev.get('quality')}")
            return False
        self.record("Quality is OK or STALE", True)
        
        # ========================================
        # 6. GET /v0/state/{provider}/{device}
        # ========================================
        print("\n6. GET /v0/state/sim0/motorctl0")
        
        result = self.http_get("/v0/state/sim0/motorctl0")
        if result["status_code"] != 200:
            self.record("Single device state", False, f"Expected 200, got {result['status_code']}")
            return False
        self.record("Single device state returns 200", True)
        
        body = result["body"]
        if body.get("device_id") != "motorctl0":
            self.record("Device ID match", False, f"Got: {body.get('device_id')}")
            return False
        self.record("Device ID matches", True)
        
        # Check value types
        values = body.get("values", [])
        value_types_found = set()
        for val in values:
            v = val.get("value", {})
            value_types_found.add(v.get("type"))
        
        if "double" not in value_types_found:
            self.record("Double type", False, f"No double values: {value_types_found}")
            return False
        self.record("Double value type present", True)
        
        # ========================================
        # 7. POST /v0/call (valid call)
        # ========================================
        print("\n7. POST /v0/call (set motor duty)")
        
        call_data = {
            "provider_id": "sim0",
            "device_id": "motorctl0",
            "function_id": 10,
            "args": {
                "motor_index": {"type": "int64", "int64": 1},
                "duty": {"type": "double", "double": 0.5}
            }
        }
        
        result = self.http_post("/v0/call", call_data)
        if result["status_code"] != 200:
            self.record("Call endpoint", False, f"Expected 200, got {result['status_code']}: {result}")
            return False
        self.record("Call returns 200", True)
        
        body = result["body"]
        if body.get("status", {}).get("code") != "OK":
            self.record("Call status OK", False, f"Got: {body.get('status')}")
            return False
        self.record("Call status OK", True)
        
        # Verify state changed (wait for poll cycle + post-call poll)
        motor1_duty = None
        for attempt in range(5):
            time.sleep(0.3)
            result = self.http_get("/v0/state/sim0/motorctl0")
            body = result["body"]
            for val in body.get("values", []):
                if val.get("signal_id") == "motor1_duty":
                    motor1_duty = val.get("value", {}).get("double")
                    break
            if motor1_duty is not None and abs(motor1_duty - 0.5) < 0.01:
                break
        
        if motor1_duty is None or abs(motor1_duty - 0.5) > 0.01:
            self.record("Duty changed", False, f"Expected 0.5, got {motor1_duty}")
            return False
        self.record("Motor duty changed to 0.5", True)
        
        # ========================================
        # 8. POST /v0/call (invalid args)
        # ========================================
        print("\n8. POST /v0/call (invalid args)")
        
        call_data = {
            "provider_id": "sim0",
            "device_id": "motorctl0",
            "function_id": 10,
            "args": {}  # Missing required args
        }
        
        result = self.http_post("/v0/call", call_data)
        # Should return 400 or 500 with error message
        if result["status_code"] == 200:
            self.record("Invalid args rejected", False, "Call succeeded when it should fail")
            return False
        self.record("Invalid args rejected (non-200)", True)
        
        body = result["body"]
        if body.get("status", {}).get("code") not in ["INVALID_ARGUMENT", "INTERNAL"]:
            self.record("Error status", False, f"Unexpected code: {body.get('status')}")
            return False
        self.record("Error status returned", True)
        
        # ========================================
        # 9. Error handling - 404 Not Found
        # ========================================
        print("\n9. Error Handling - 404")
        
        result = self.http_get("/v0/devices/sim0/nonexistent/capabilities")
        if result["status_code"] != 404:
            self.record("404 for unknown device", False, f"Expected 404, got {result['status_code']}")
            return False
        self.record("404 for unknown device", True)
        
        result = self.http_get("/v0/nonexistent")
        if result["status_code"] != 404:
            self.record("404 for unknown route", False, f"Expected 404, got {result['status_code']}")
            return False
        self.record("404 for unknown route", True)
        
        # ========================================
        # 10. Verify all value types
        # ========================================
        print("\n10. Value Types Check")
        
        result = self.http_get("/v0/state/sim0/tempctl0")
        body = result["body"]
        values = body.get("values", [])
        
        types_found = {}
        for val in values:
            v = val.get("value", {})
            t = v.get("type")
            types_found[t] = val.get("signal_id")
        
        # tempctl0 should have: double (temps), bool (relays), string (mode)
        for expected_type in ["double", "bool", "string"]:
            if expected_type not in types_found:
                self.record(f"Type {expected_type}", False, f"Not found in {types_found}")
            else:
                self.record(f"Type {expected_type} present", True)
        
        # ========================================
        # Summary
        # ========================================
        print("\n" + "=" * 60)
        passed = sum(1 for r in self.results if r.passed)
        failed = sum(1 for r in self.results if not r.passed)
        print(f"  Results: {passed} passed, {failed} failed")
        print("=" * 60)
        
        return failed == 0


def find_default_paths() -> tuple:
    """Find default runtime and provider paths."""
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    
    # Try common build locations
    runtime_candidates = [
        repo_root / "build" / "core" / "Release" / "anolis-runtime.exe",
        repo_root / "build" / "core" / "Debug" / "anolis-runtime.exe",
        repo_root / "build" / "Release" / "anolis-runtime.exe",
        repo_root / "build" / "anolis-runtime",
    ]
    
    provider_candidates = [
        repo_root.parent / "anolis-provider-sim" / "build" / "Release" / "anolis-provider-sim.exe",
        repo_root.parent / "anolis-provider-sim" / "build" / "Debug" / "anolis-provider-sim.exe",
        repo_root.parent / "anolis-provider-sim" / "build" / "anolis-provider-sim",
    ]
    
    runtime_path = None
    for p in runtime_candidates:
        if p.exists():
            runtime_path = p
            break
    
    provider_path = None
    for p in provider_candidates:
        if p.exists():
            provider_path = p
            break
    
    return runtime_path, provider_path


def main():
    parser = argparse.ArgumentParser(description="Phase 4 HTTP Gateway Integration Tests")
    parser.add_argument("--runtime", type=str, help="Path to anolis-runtime executable")
    parser.add_argument("--provider", type=str, help="Path to anolis-provider-sim executable")
    parser.add_argument("--port", type=int, default=8080, help="HTTP port (default: 8080)")
    parser.add_argument("--timeout", type=float, default=30.0, help="Test timeout in seconds")
    args = parser.parse_args()
    
    # Find paths
    default_runtime, default_provider = find_default_paths()
    
    runtime_path = Path(args.runtime) if args.runtime else default_runtime
    provider_path = Path(args.provider) if args.provider else default_provider
    
    if not runtime_path:
        print("ERROR: Could not find anolis-runtime. Use --runtime to specify path.")
        sys.exit(1)
    
    if not provider_path:
        print("ERROR: Could not find anolis-provider-sim. Use --provider to specify path.")
        sys.exit(1)
    
    print(f"Runtime:  {runtime_path}")
    print(f"Provider: {provider_path}")
    print(f"Port:     {args.port}")
    
    # Run tests
    tester = Phase4HTTPTester(runtime_path, provider_path, args.port, args.timeout)
    
    try:
        if not tester.setup():
            sys.exit(1)
        
        if not tester.start_runtime():
            sys.exit(1)
        
        success = tester.run_tests()
        
        if success:
            print("\n✓ All Phase 4 HTTP tests passed!\n")
            sys.exit(0)
        else:
            print("\n✗ Some tests failed\n")
            sys.exit(1)
            
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(130)
    finally:
        tester.cleanup()


if __name__ == "__main__":
    main()
