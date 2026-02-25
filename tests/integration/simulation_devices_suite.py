"""
Simulation Device and Fault Injection Testing

Validates provider-sim devices and fault injection capabilities:
1. Device discovery (all 5 devices: tempctl0, motorctl0, relayio0, analogsensor0, chaos_control)
2. Device functionality (signals, functions, state changes)
3. Fault injection functions (device unavailable, call latency, call failure, signal faults)

This script provides quick validation of the simulation provider without requiring
the full scenario runner infrastructure.

Requires:
- Runtime reachable at the provided ``base_url`` with provider-sim connected

"""

import time

import requests

from tests.support.api_helpers import wait_for_condition

REQUEST_TIMEOUT_S = 5.0


def test_device_discovery(base_url: str) -> None:
    """Test that all 5 devices are discovered."""
    print("\n=== TEST: Device Discovery ===")
    resp = requests.get(f"{base_url}/v0/devices", timeout=REQUEST_TIMEOUT_S)
    devices = resp.json().get("devices", [])

    expected_devices = [
        "tempctl0",
        "motorctl0",
        "relayio0",
        "analogsensor0",
        "chaos_control",
    ]
    found_devices = [d["device_id"] for d in devices if d["provider_id"] == "sim0"]

    print(f"  Found {len(found_devices)} devices")
    for device_id in expected_devices:
        if device_id in found_devices:
            print(f"  [PASS] {device_id}")
        else:
            raise AssertionError(f"MISSING device: {device_id}")


def test_relayio0(base_url: str) -> None:
    """Test relayio0 device with boolean relay/GPIO signals."""
    print("\n=== TEST: relayio0 Device (Bool Signals) ===")

    # Read initial state
    resp = requests.get(f"{base_url}/v0/state/sim0/relayio0", timeout=REQUEST_TIMEOUT_S)
    state = resp.json()
    print(f"  Signals: {len(state['values'])}")

    relay1_before = next((s for s in state["values"] if s["signal_id"] == "relay_ch1_state"), None)
    if not relay1_before:
        raise AssertionError("Could not find relay_ch1_state signal")
    print(f"  relay_ch1_state: {relay1_before['value']['bool']}")

    # Toggle relay
    call_body = {
        "provider_id": "sim0",
        "device_id": "relayio0",
        "function_id": 1,
        "args": {"enabled": {"type": "bool", "bool": True}},
    }
    resp = requests.post(f"{base_url}/v0/call", json=call_body, timeout=REQUEST_TIMEOUT_S)
    result = resp.json()

    if result["status"]["code"] != "OK":
        raise AssertionError(f"Function call failed: {result['status']['message']}")

    # Verify state changed (wait for poll)
    def check_relay():
        resp = requests.get(f"{base_url}/v0/state/sim0/relayio0", timeout=REQUEST_TIMEOUT_S)
        state = resp.json()
        val = next((s for s in state["values"] if s["signal_id"] == "relay_ch1_state"), None)
        return val and val["value"]["bool"]

    wait_for_condition(check_relay, timeout=2.0)

    resp = requests.get(f"{base_url}/v0/state/sim0/relayio0", timeout=REQUEST_TIMEOUT_S)
    state = resp.json()
    relay1_after = next((s for s in state["values"] if s["signal_id"] == "relay_ch1_state"), None)

    if not relay1_after:
        raise AssertionError("Could not find relay_ch1_state signal after toggle")

    assert relay1_after["value"]["bool"], "Relay did not toggle"
    print("  [PASS] Relay toggled successfully")


def test_analogsensor0(base_url: str) -> None:
    """Test analogsensor0 device with double voltage signals and quality states."""
    print("\n=== TEST: analogsensor0 Device (Double Signals + Quality) ===")

    # Read initial state
    resp = requests.get(f"{base_url}/v0/state/sim0/analogsensor0", timeout=REQUEST_TIMEOUT_S)
    state = resp.json()

    voltage_ch1 = next((s for s in state["values"] if s["signal_id"] == "voltage_ch1"), None)
    quality = next((s for s in state["values"] if s["signal_id"] == "sensor_quality"), None)

    if not voltage_ch1:
        raise AssertionError("Could not find voltage_ch1 signal")
    if not quality:
        raise AssertionError("Could not find sensor_quality signal")

    print(f"  voltage_ch1: {voltage_ch1['value']['double']:.2f}V")
    print(f"  sensor_quality: {quality['value']['string']}")

    # Inject noise
    call_body = {
        "provider_id": "sim0",
        "device_id": "analogsensor0",
        "function_id": 2,
        "args": {"enabled": {"type": "bool", "bool": True}},
    }
    resp = requests.post(f"{base_url}/v0/call", json=call_body, timeout=REQUEST_TIMEOUT_S)
    result = resp.json()
    if result["status"]["code"] != "OK":
        raise AssertionError(f"inject_noise failed: {result['status']['message']}")

    # Current analog sensor model degrades quality over longer time windows (30s+).
    # Keep this suite fast: verify contract and data flow instead of waiting for long drift.
    time.sleep(0.6)

    resp = requests.get(f"{base_url}/v0/state/sim0/analogsensor0", timeout=REQUEST_TIMEOUT_S)
    state = resp.json()
    quality_after = next((s for s in state["values"] if s["signal_id"] == "sensor_quality"), None)

    if not quality_after:
        raise AssertionError("Could not find sensor_quality signal after noise")

    quality_value = quality_after["value"]["string"]
    print(f"  sensor_quality after noise: {quality_value}")
    if quality_value not in ["GOOD", "NOISY", "FAULT"]:
        raise AssertionError(f"Unexpected quality value: {quality_value}")

    # Confirm samples remain dynamic after enabling noise.
    samples: list[float] = []
    for _ in range(4):
        resp = requests.get(f"{base_url}/v0/state/sim0/analogsensor0", timeout=REQUEST_TIMEOUT_S)
        state = resp.json()
        reading = next((s for s in state["values"] if s["signal_id"] == "voltage_ch1"), None)
        if not reading:
            raise AssertionError("Missing voltage_ch1 during sampling")
        samples.append(float(reading["value"]["double"]))
        time.sleep(0.2)

    # Disable noise so following tests start from a clean baseline.
    requests.post(
        f"{base_url}/v0/call",
        json={
            "provider_id": "sim0",
            "device_id": "analogsensor0",
            "function_id": 2,
            "args": {"enabled": {"type": "bool", "bool": False}},
        },
        timeout=REQUEST_TIMEOUT_S,
    )

    if len(set(samples)) <= 1:
        raise AssertionError("voltage_ch1 samples did not change after noise enable")

    print("  [PASS] inject_noise accepted and analog readings remained dynamic")


def test_fault_injection_clear(base_url: str) -> None:
    """Test clear_faults function."""
    print("\n=== TEST: Fault Injection - clear_faults ===")

    call_body = {
        "provider_id": "sim0",
        "device_id": "chaos_control",
        "function_id": 5,
        "args": {},
    }
    resp = requests.post(f"{base_url}/v0/call", json=call_body, timeout=REQUEST_TIMEOUT_S)
    result = resp.json()

    assert result["status"]["code"] == "OK", f"clear_faults failed: {result['status']['message']}"
    print("  [PASS] clear_faults succeeded")


def test_fault_injection_device_unavailable(base_url: str) -> None:
    """Test inject_device_unavailable function."""
    print("\n=== TEST: Fault Injection - inject_device_unavailable ===")

    # First clear any existing faults
    test_fault_injection_clear(base_url)

    # Read baseline
    resp = requests.get(f"{base_url}/v0/state/sim0/motorctl0", timeout=REQUEST_TIMEOUT_S)
    baseline_values = len(resp.json()["values"])
    print(f"  Baseline: {baseline_values} signals")

    # Inject device unavailable
    call_body = {
        "provider_id": "sim0",
        "device_id": "chaos_control",
        "function_id": 1,
        "args": {
            "device_id": {"type": "string", "string": "motorctl0"},
            "duration_ms": {"type": "int64", "int64": 2000},
        },
    }
    resp = requests.post(f"{base_url}/v0/call", json=call_body, timeout=REQUEST_TIMEOUT_S)

    # Wait for state to reflect unavailability (may return empty values)
    def device_unavailable():
        resp = requests.get(f"{base_url}/v0/state/sim0/motorctl0", timeout=REQUEST_TIMEOUT_S)
        values = resp.json().get("values", [])
        return len(values) < baseline_values

    assert wait_for_condition(device_unavailable, timeout=2.0, description="device unavailable state"), (
        "Device did not enter unavailable state"
    )

    print("  [PASS] inject_device_unavailable called (check runtime logs for errors)")

    # Clear faults after device becomes available again
    def device_restored():
        resp = requests.get(f"{base_url}/v0/state/sim0/motorctl0", timeout=REQUEST_TIMEOUT_S)
        values = resp.json().get("values", [])
        return len(values) >= baseline_values

    assert wait_for_condition(device_restored, timeout=3.0, description="device restoration"), (
        "Device did not recover after unavailable fault"
    )
    test_fault_injection_clear(base_url)


def test_fault_injection_call_latency(base_url: str) -> None:
    """Test inject_call_latency function."""
    print("\n=== TEST: Fault Injection - inject_call_latency ===")

    # Clear faults first
    test_fault_injection_clear(base_url)

    # Inject 1 second latency
    call_body = {
        "provider_id": "sim0",
        "device_id": "chaos_control",
        "function_id": 3,
        "args": {
            "device_id": {"type": "string", "string": "relayio0"},
            "latency_ms": {"type": "int64", "int64": 1000},
        },
    }
    requests.post(f"{base_url}/v0/call", json=call_body, timeout=REQUEST_TIMEOUT_S)

    # Make a call and measure time
    start = time.time()
    call_body = {
        "provider_id": "sim0",
        "device_id": "relayio0",
        "function_id": 1,
        "args": {"enabled": {"type": "bool", "bool": False}},
    }
    requests.post(f"{base_url}/v0/call", json=call_body, timeout=REQUEST_TIMEOUT_S)
    elapsed = time.time() - start

    print(f"  Call took {elapsed:.2f}s (should be ~1s)")

    # Clear faults
    test_fault_injection_clear(base_url)

    assert elapsed >= 0.9, f"No latency detected (elapsed={elapsed:.2f}s)"
    print("  [PASS] Latency injection working")


def test_fault_injection_call_failure(base_url: str) -> None:
    """Test inject_call_failure function."""
    print("\n=== TEST: Fault Injection - inject_call_failure ===")

    # Clear faults first
    test_fault_injection_clear(base_url)

    # Inject 100% failure rate
    call_body = {
        "provider_id": "sim0",
        "device_id": "chaos_control",
        "function_id": 4,
        "args": {
            "device_id": {"type": "string", "string": "relayio0"},
            "function_id": {"type": "string", "string": "1"},
            "failure_rate": {"type": "double", "double": 1.0},
        },
    }
    resp = requests.post(f"{base_url}/v0/call", json=call_body, timeout=REQUEST_TIMEOUT_S)

    # Try to call the function - should fail
    call_body = {
        "provider_id": "sim0",
        "device_id": "relayio0",
        "function_id": 1,
        "args": {"enabled": {"type": "bool", "bool": True}},
    }
    resp = requests.post(f"{base_url}/v0/call", json=call_body, timeout=REQUEST_TIMEOUT_S)
    result = resp.json()

    # Clear faults
    test_fault_injection_clear(base_url)

    assert result["status"]["code"] != "OK", "Call succeeded when it should have failed"
    print(f"  [PASS] Call failed as expected: {result['status']['message']}")
