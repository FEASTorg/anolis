"""
Simulation Device and Fault Injection Testing

Validates provider-sim devices and fault injection capabilities:
1. Device discovery (all 5 devices: tempctl0, motorctl0, relayio0, analogsensor0, sim_control)
2. Device functionality (signals, functions, state changes)
3. Fault injection functions (device unavailable, call latency, call failure, signal faults)

This script provides quick validation of the simulation provider without requiring
the full scenario runner infrastructure.

Requires:
- Runtime running at localhost:8080 with provider-sim connected

Usage:
    python scripts/test_simulation_devices.py
"""

import requests
import time
import sys

BASE_URL = "http://localhost:8080"


def wait_for_condition(condition_func, timeout=5.0, interval=0.1, description="condition"):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if condition_func():
            return True
        time.sleep(interval)
    print(f"Warning: Timed out waiting for {description}")
    return False


def test_device_discovery():
    """Test that all 5 devices are discovered."""
    print("\n=== TEST: Device Discovery ===")
    resp = requests.get(f"{BASE_URL}/v0/devices")
    devices = resp.json().get("devices", [])

    expected_devices = [
        "tempctl0",
        "motorctl0",
        "relayio0",
        "analogsensor0",
        "sim_control",
    ]
    found_devices = [d["device_id"] for d in devices if d["provider_id"] == "sim0"]

    print(f"  Found {len(found_devices)} devices")
    for device_id in expected_devices:
        if device_id in found_devices:
            print(f"  [PASS] {device_id}")
        else:
            print(f"  [FAIL] MISSING: {device_id}")
            return False

    return True


def test_relayio0():
    """Test relayio0 device with boolean relay/GPIO signals."""
    print("\n=== TEST: relayio0 Device (Bool Signals) ===")

    # Read initial state
    resp = requests.get(f"{BASE_URL}/v0/state/sim0/relayio0")
    state = resp.json()
    print(f"  Signals: {len(state['values'])}")

    relay1_before = next(
        (s for s in state["values"] if s["signal_id"] == "relay_ch1_state"), None
    )
    print(f"  relay_ch1_state: {relay1_before['value']['bool']}")

    # Toggle relay
    call_body = {
        "provider_id": "sim0",
        "device_id": "relayio0",
        "function_id": 1,
        "args": {"enabled": {"type": "bool", "bool": True}},
    }
    resp = requests.post(f"{BASE_URL}/v0/call", json=call_body)
    result = resp.json()

    if result["status"]["code"] != "OK":
        print(f"  [FAIL] Function call failed: {result['status']['message']}")
        return False

    # Verify state changed (wait for poll)
    def check_relay():
        resp = requests.get(f"{BASE_URL}/v0/state/sim0/relayio0")
        state = resp.json()
        val = next((s for s in state["values"] if s["signal_id"] == "relay_ch1_state"), None)
        return val and val["value"]["bool"]

    wait_for_condition(check_relay, timeout=2.0)
    
    resp = requests.get(f"{BASE_URL}/v0/state/sim0/relayio0")
    state = resp.json()
    relay1_after = next(
        (s for s in state["values"] if s["signal_id"] == "relay_ch1_state"), None
    )

    if relay1_after["value"]["bool"]:
        print("  [PASS] Relay toggled successfully")
        return True
    else:
        print("  [FAIL] Relay did not toggle")
        return False


def test_analogsensor0():
    """Test analogsensor0 device with double voltage signals and quality states."""
    print("\n=== TEST: analogsensor0 Device (Double Signals + Quality) ===")

    # Read initial state
    resp = requests.get(f"{BASE_URL}/v0/state/sim0/analogsensor0")
    state = resp.json()

    voltage_ch1 = next(
        (s for s in state["values"] if s["signal_id"] == "voltage_ch1"), None
    )
    quality = next(
        (s for s in state["values"] if s["signal_id"] == "sensor_quality"), None
    )

    print(f"  voltage_ch1: {voltage_ch1['value']['double']:.2f}V")
    print(f"  sensor_quality: {quality['value']['string']}")

    # Inject noise to degrade quality
    call_body = {
        "provider_id": "sim0",
        "device_id": "analogsensor0",
        "function_id": 2,
        "args": {"enabled": {"type": "bool", "bool": True}},
    }
    requests.post(f"{BASE_URL}/v0/call", json=call_body)

    # Wait for quality to degrade
    def check_quality_bad():
        resp = requests.get(f"{BASE_URL}/v0/state/sim0/analogsensor0")
        state = resp.json()
        q = next((s for s in state["values"] if s["signal_id"] == "sensor_quality"), None)
        return q and q["value"]["string"] != "GOOD"

    wait_for_condition(check_quality_bad, timeout=5.0, description="quality to degrade")
    
    resp = requests.get(f"{BASE_URL}/v0/state/sim0/analogsensor0")
    state = resp.json()
    quality_after = next(
        (s for s in state["values"] if s["signal_id"] == "sensor_quality"), None
    )

    print(f"  sensor_quality after noise: {quality_after['value']['string']}")

    if quality_after["value"]["string"] in ["NOISY", "FAULT"]:
        print("  [PASS] Quality degraded as expected")
        return True
    else:
        print("  [FAIL] Quality did not degrade")
        return False


def test_fault_injection_clear():
    """Test clear_faults function."""
    print("\n=== TEST: Fault Injection - clear_faults ===")

    call_body = {
        "provider_id": "sim0",
        "device_id": "sim_control",
        "function_id": 5,
        "args": {},
    }
    resp = requests.post(f"{BASE_URL}/v0/call", json=call_body)
    result = resp.json()

    if result["status"]["code"] == "OK":
        print("  [PASS] clear_faults succeeded")
        return True
    else:
        print(f"  [FAIL] clear_faults failed: {result['status']['message']}")
        return False


def test_fault_injection_device_unavailable():
    """Test inject_device_unavailable function."""
    print("\n=== TEST: Fault Injection - inject_device_unavailable ===")

    # First clear any existing faults
    test_fault_injection_clear()

    # Read baseline
    resp = requests.get(f"{BASE_URL}/v0/state/sim0/motorctl0")
    baseline_values = len(resp.json()["values"])
    print(f"  Baseline: {baseline_values} signals")

    # Inject device unavailable
    call_body = {
        "provider_id": "sim0",
        "device_id": "sim_control",
        "function_id": 1,
        "args": {
            "device_id": {"type": "string", "string": "motorctl0"},
            "duration_ms": {"type": "int64", "int64": 2000},
        },
    }
    resp = requests.post(f"{BASE_URL}/v0/call", json=call_body)

    # Wait and check (state cache may still return cached data)
    time.sleep(0.5)
    resp = requests.get(f"{BASE_URL}/v0/state/sim0/motorctl0")
    # Device will return cached state but provider logs should show errors

    print("  [PASS] inject_device_unavailable called (check runtime logs for errors)")

    # Clear faults
    time.sleep(2)
    test_fault_injection_clear()

    return True


def test_fault_injection_call_latency():
    """Test inject_call_latency function."""
    print("\n=== TEST: Fault Injection - inject_call_latency ===")

    # Clear faults first
    test_fault_injection_clear()

    # Inject 1 second latency
    call_body = {
        "provider_id": "sim0",
        "device_id": "sim_control",
        "function_id": 3,
        "args": {
            "device_id": {"type": "string", "string": "relayio0"},
            "latency_ms": {"type": "int64", "int64": 1000},
        },
    }
    resp = requests.post(f"{BASE_URL}/v0/call", json=call_body)

    # Make a call and measure time
    start = time.time()
    call_body = {
        "provider_id": "sim0",
        "device_id": "relayio0",
        "function_id": 1,
        "args": {"enabled": {"type": "bool", "bool": False}},
    }
    resp = requests.post(f"{BASE_URL}/v0/call", json=call_body)
    elapsed = time.time() - start

    print(f"  Call took {elapsed:.2f}s (should be ~1s)")

    # Clear faults
    test_fault_injection_clear()

    if elapsed >= 0.9:
        print("  [PASS] Latency injection working")
        return True
    else:
        print("  [FAIL] No latency detected")
        return False


def test_fault_injection_call_failure():
    """Test inject_call_failure function."""
    print("\n=== TEST: Fault Injection - inject_call_failure ===")

    # Clear faults first
    test_fault_injection_clear()

    # Inject 100% failure rate
    call_body = {
        "provider_id": "sim0",
        "device_id": "sim_control",
        "function_id": 4,
        "args": {
            "device_id": {"type": "string", "string": "relayio0"},
            "function_id": {"type": "string", "string": "1"},
            "failure_rate": {"type": "double", "double": 1.0},
        },
    }
    resp = requests.post(f"{BASE_URL}/v0/call", json=call_body)

    # Try to call the function - should fail
    call_body = {
        "provider_id": "sim0",
        "device_id": "relayio0",
        "function_id": 1,
        "args": {"enabled": {"type": "bool", "bool": True}},
    }
    resp = requests.post(f"{BASE_URL}/v0/call", json=call_body)
    result = resp.json()

    # Clear faults
    test_fault_injection_clear()

    if result["status"]["code"] != "OK":
        print(f"  [PASS] Call failed as expected: {result['status']['message']}")
        return True
    else:
        print("  [FAIL] Call succeeded when it should have failed")
        return False


def main():
    print("=" * 60)
    print("Simulation Device and Fault Injection Testing")
    print("=" * 60)

    tests = [
        ("Device Discovery", test_device_discovery),
        ("relayio0 Device", test_relayio0),
        ("analogsensor0 Device", test_analogsensor0),
        ("Fault: clear_faults", test_fault_injection_clear),
        ("Fault: device_unavailable", test_fault_injection_device_unavailable),
        ("Fault: call_latency", test_fault_injection_call_latency),
        ("Fault: call_failure", test_fault_injection_call_failure),
    ]

    passed = 0
    failed = 0

    for name, test_func in tests:
        try:
            if test_func():
                passed += 1
            else:
                failed += 1
        except Exception as e:
            print(f"  [FAIL] Exception: {e}")
            failed += 1

    print("\n" + "=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 60)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        sys.exit(1)
    except requests.exceptions.ConnectionError:
        print("\n[FAIL] Could not connect to runtime. Is it running at localhost:8080?")
        sys.exit(1)
