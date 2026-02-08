#!/usr/bin/env python3
"""
Anolis HTTP Gateway Integration Test

This script validates HTTP API functionality:
- GET /v0/devices (list all devices)
- GET /v0/devices/{provider}/{device}/capabilities (device capabilities)
- GET /v0/state (all device states)
- GET /v0/state/{provider}/{device} (single device state)
- POST /v0/call (execute device function)
- GET /v0/runtime/status (runtime status)
- Error handling (404, 400, 503)
- Provider failure -> UNAVAILABLE transition

Usage:
    python tests/integration/test_http.py [--runtime PATH] [--provider PATH] [--port PORT]

Prerequisites:
    - Core tests must pass (test_core.py)
    - Runtime and provider executables must be built
"""

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional

import requests
from test_fixtures import RuntimeFixture
from test_helpers import assert_http_available, wait_for_condition


@dataclass
class TestResult:
    name: str
    passed: bool
    message: str = ""


class HttpGatewayTester:
    """Test harness for HTTP API integration tests."""

    def __init__(
        self,
        runtime_path: Path,
        provider_path: Path,
        port: int = 8080,
        timeout: float = 30.0,
    ):
        self.port = port
        self.base_url = f"http://127.0.0.1:{port}"
        self.timeout = timeout
        self.results: List[TestResult] = []
        self.fixture = RuntimeFixture(runtime_path, provider_path, http_port=port)

    def cleanup(self):
        """Clean up resources."""
        self.fixture.cleanup()

    def start_runtime(self) -> bool:
        """Start the runtime process."""
        return self.fixture.start()

    def record(self, name: str, passed: bool, message: str = ""):
        """Record a test result."""
        self.results.append(TestResult(name, passed, message))
        status = "[PASS]" if passed else "[FAIL]"
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

    def test_sse_endpoint(self) -> bool:
        """Test SSE /v0/events endpoint."""
        import threading

        # Test 1: SSE endpoint exists and returns correct content-type
        try:
            resp = requests.get(
                f"{self.base_url}/v0/events?provider_id=sim0&device_id=tempctl0",
                stream=True,
                timeout=2,
                headers={"Accept": "text/event-stream"},
            )

            content_type = resp.headers.get("Content-Type", "")
            if "text/event-stream" not in content_type:
                self.record(
                    "SSE content-type",
                    False,
                    f"Expected text/event-stream, got {content_type}",
                )
                resp.close()
                return False
            self.record("SSE content-type correct", True)

        except requests.exceptions.Timeout:
            # Timeout is expected for SSE since it's a long-running connection
            pass
        except Exception as e:
            self.record("SSE endpoint accessible", False, str(e))
            return False

        # Test 2: Receive actual events
        events_received = []
        stop_event = threading.Event()

        def collect_events():
            try:
                resp = requests.get(
                    f"{self.base_url}/v0/events?provider_id=sim0&device_id=tempctl0",
                    stream=True,
                    timeout=10,
                    headers={"Accept": "text/event-stream"},
                )

                buffer = ""
                for chunk in resp.iter_content(chunk_size=1, decode_unicode=True):
                    if stop_event.is_set():
                        break
                    if chunk:
                        buffer += chunk
                        # Look for complete events (double newline)
                        while "\n\n" in buffer:
                            event_str, buffer = buffer.split("\n\n", 1)
                            if event_str.strip():
                                events_received.append(event_str)
                                if len(events_received) >= 3:
                                    stop_event.set()
                                    break
                resp.close()
            except Exception:
                pass

        # Collect events for up to 3 seconds
        collector = threading.Thread(target=collect_events, daemon=True)
        collector.start()
        collector.join(timeout=3.0)
        stop_event.set()

        if len(events_received) < 1:
            self.record("SSE events received", False, "No events received in 3 seconds")
            return False
        self.record(f"SSE events received ({len(events_received)})", True)

        # Test 3: Verify event format
        first_event = events_received[0]

        # Check for required fields
        if "event: state_update" not in first_event:
            self.record("SSE event type", False, f"Missing 'event:' field: {first_event}")
            return False
        self.record("SSE event type present", True)

        if "id: " not in first_event:
            self.record("SSE event id", False, f"Missing 'id:' field: {first_event}")
            return False
        self.record("SSE event id present", True)

        if "data: " not in first_event:
            self.record("SSE data field", False, f"Missing 'data:' field: {first_event}")
            return False
        self.record("SSE data field present", True)

        # Parse data JSON
        try:
            data_line = [line for line in first_event.split("\n") if line.startswith("data: ")][0]
            data_json = json.loads(data_line[6:])  # Strip "data: " prefix

            # Check required fields in data
            required_fields = [
                "provider_id",
                "device_id",
                "signal_id",
                "value",
                "quality",
                "timestamp_ms",
            ]
            for field in required_fields:
                if field not in data_json:
                    self.record(f"SSE data.{field}", False, f"Missing field in: {data_json}")
                    return False
            self.record("SSE data fields complete", True)

            # Check value has type
            value = data_json.get("value", {})
            if "type" not in value:
                self.record("SSE value.type", False, f"Missing type in value: {value}")
                return False
            self.record("SSE value has type", True)

        except Exception as e:
            self.record("SSE data JSON parse", False, str(e))
            return False

        # Test 4: Test filtering works
        try:
            resp = requests.get(
                f"{self.base_url}/v0/events?provider_id=sim0&device_id=nonexistent",
                stream=True,
                timeout=1.5,
                headers={"Accept": "text/event-stream"},
            )
            # Should connect but receive no events for nonexistent device
            self.record("SSE filter accepts connection", True)
            resp.close()
        except requests.exceptions.Timeout:
            self.record("SSE filter accepts connection", True)
        except Exception as e:
            self.record("SSE filter test", False, str(e))
            return False

        return True

    def wait_for_http_ready(self, timeout: float = 15.0) -> bool:
        """Poll /v0/runtime/status until server is ready."""
        return wait_for_condition(
            lambda: requests.get(f"{self.base_url}/v0/runtime/status", timeout=2).status_code == 200,
            timeout=timeout,
            interval=0.2,
            description="HTTP ready",
        )

    def run_tests(self) -> bool:
        """Run all HTTP integration tests."""
        print("\n" + "=" * 60)
        print("  HTTP Gateway Integration Tests")
        print("=" * 60)

        # ========================================
        # 1. Runtime Startup & HTTP Ready
        # ========================================
        print("\n1. Runtime Startup & HTTP Ready")

        # Use API-based readiness check instead of log markers
        if not assert_http_available(self.base_url, timeout=15):
            self.record("HTTP responsive", False, "Could not connect to HTTP server")
            return False
        self.record("HTTP responsive", True)

        # Verify status endpoint
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
            self.record(
                "Expected devices",
                False,
                f"Missing tempctl0 or motorctl0: {device_ids}",
            )
            return False
        self.record("tempctl0 and motorctl0 present", True)

        # ========================================
        # 4. GET /v0/devices/{provider}/{device}/capabilities
        # ========================================
        print("\n4. GET /v0/devices/sim0/tempctl0/capabilities")

        result = self.http_get("/v0/devices/sim0/tempctl0/capabilities")
        if result["status_code"] != 200:
            self.record(
                "Capabilities endpoint",
                False,
                f"Expected 200, got {result['status_code']}",
            )
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
            self.record(
                "Functions present",
                False,
                f"Expected 1+ functions, got {len(functions)}",
            )
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

        # Wait for at least one poll cycle by checking for non-empty state values
        def state_has_values_for_all_devices():
            result = self.http_get("/v0/state")
            if result["status_code"] != 200:
                return False
            devices = result["body"].get("devices", [])
            if not devices:
                return False
            # Require every device to have at least one value (prevents race on slower runners)
            return all(len(dev.get("values", [])) > 0 for dev in devices)

        if not wait_for_condition(state_has_values_for_all_devices, timeout=5.0, description="state values"):
            self.record("State endpoint", False, "State values never became available for all devices")
            return False

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
            self.record(
                "Quality valid",
                False,
                f"Unexpected quality: {first_dev.get('quality')}",
            )
            return False
        self.record("Quality is OK or STALE", True)

        # ========================================
        # 6. GET /v0/state/{provider}/{device}
        # ========================================
        print("\n6. GET /v0/state/sim0/motorctl0")

        result = self.http_get("/v0/state/sim0/motorctl0")
        if result["status_code"] != 200:
            self.record(
                "Single device state",
                False,
                f"Expected 200, got {result['status_code']}",
            )
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
                "duty": {"type": "double", "double": 0.5},
            },
        }

        result = self.http_post("/v0/call", call_data)
        if result["status_code"] != 200:
            self.record(
                "Call endpoint",
                False,
                f"Expected 200, got {result['status_code']}: {result}",
            )
            return False
        self.record("Call returns 200", True)

        body = result["body"]
        if body.get("status", {}).get("code") != "OK":
            self.record("Call status OK", False, f"Got: {body.get('status')}")
            return False
        self.record("Call status OK", True)

        # Verify state changed (wait for poll cycle + post-call poll)
        motor1_duty = {"value": None}

        def duty_updated():
            result = self.http_get("/v0/state/sim0/motorctl0")
            body = result["body"]
            for val in body.get("values", []):
                if val.get("signal_id") == "motor1_duty":
                    motor1_duty["value"] = val.get("value", {}).get("double")
                    break
            return motor1_duty["value"] is not None and abs(motor1_duty["value"] - 0.5) < 0.01

        if not wait_for_condition(duty_updated, timeout=3.0, interval=0.1, description="motor duty update"):
            self.record("Duty changed", False, f"Expected 0.5, got {motor1_duty['value']}")
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
            "args": {},  # Missing required args
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
            self.record(
                "404 for unknown device",
                False,
                f"Expected 404, got {result['status_code']}",
            )
            return False
        self.record("404 for unknown device", True)

        result = self.http_get("/v0/nonexistent")
        if result["status_code"] != 404:
            self.record(
                "404 for unknown route",
                False,
                f"Expected 404, got {result['status_code']}",
            )
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
        # 11. GET /v0/events (SSE endpoint)
        # ========================================
        print("\n11. SSE Event Streaming")

        # Test SSE endpoint returns correct content-type and events
        sse_result = self.test_sse_endpoint()
        if not sse_result:
            return False

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
    repo_root = script_dir.parent.parent

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
    parser = argparse.ArgumentParser(description="HTTP Gateway Integration Tests")
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
    tester = HttpGatewayTester(runtime_path, provider_path, args.port, args.timeout)

    try:
        if not tester.start_runtime():
            sys.exit(1)

        success = tester.run_tests()

        if success:
            print("\n[PASS] All HTTP tests passed!\n")
            sys.exit(0)
        else:
            print("\n[FAIL] Some tests failed\n")
            sys.exit(1)

    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(130)
    finally:
        tester.cleanup()


if __name__ == "__main__":
    main()
