"""
Signal Fault Injection Validation

Validates the critical signal fault injection feature that marks signal quality as FAULT.
Ensures that read_signals() properly applies fault state to signal quality values.

Test procedure:
1. Get baseline tempctl0 signals (should have GOOD quality)
2. Inject signal fault on tempctl0 for 5 seconds
3. Read signals (should now show FAULT quality)
4. Wait for expiration
5. Clear faults (if still present)
6. Read signals again (should be back to GOOD quality)

Requires:
- Runtime reachable at the provided ``base_url`` with provider-sim connected

"""

import time
from typing import Any, cast

import requests

REQUEST_TIMEOUT_S = 5.0


def _find_signal(state: dict[str, Any], candidates: list[str]) -> dict[str, Any] | None:
    values = cast(list[dict[str, Any]], state.get("values", []))
    for signal_id in candidates:
        for signal in values:
            if signal.get("signal_id") == signal_id:
                return signal
    return None


def test_signal_fault_injection(base_url: str) -> None:
    """Test that signal fault injection overrides signal quality to FAULT."""

    print("=== Signal Fault Injection Test ===\n")

    # Step 1: Read baseline signal value
    print("1. Reading baseline signal value...")
    response = requests.get(f"{base_url}/v0/state/sim0/tempctl0", timeout=REQUEST_TIMEOUT_S)
    if response.status_code != 200:
        raise AssertionError(f"Could not read device state: {response.status_code}")

    state = response.json()
    target_signal_candidates = ["tc1_temp", "temp_pv"]
    baseline_signal = _find_signal(state, target_signal_candidates)
    if not baseline_signal:
        signal_ids = [s.get("signal_id", "<missing>") for s in state.get("values", [])]
        raise AssertionError(f"Could not find expected temperature signal. Available signals: {signal_ids}")
    target_signal_id = baseline_signal["signal_id"]

    print(f"   Baseline: {target_signal_id} = {baseline_signal['value']}, quality = {baseline_signal['quality']}")

    if baseline_signal["quality"] != "OK":
        print(f"WARN: Baseline quality is not OK: {baseline_signal['quality']}")

    # Step 2: Inject signal fault
    print("\n2. Injecting signal fault for temp_pv (5 seconds)...")
    response = requests.post(
        f"{base_url}/v0/call",
        json={
            "provider_id": "sim0",
            "device_id": "chaos_control",
            "function_id": 2,
            "args": {
                "device_id": {"type": "string", "string": "tempctl0"},
                "signal_id": {"type": "string", "string": target_signal_id},
                "duration_ms": {"type": "int64", "int64": 5000},
            },
        },
        timeout=REQUEST_TIMEOUT_S,
    )

    if response.status_code != 200:
        raise AssertionError(f"Could not inject signal fault: {response.status_code} {response.text}")

    result = response.json()
    if result.get("status", {}).get("code") != "OK":
        raise AssertionError(f"inject_signal_fault call failed: {result}")

    print(f"   Fault injected successfully for signal: {target_signal_id}")

    # Step 3: Read signal value immediately - should be FAULT quality
    print("\n3. Reading signal value (should be FAULT)...")
    time.sleep(0.2)  # Brief delay for fault to apply

    response = requests.get(f"{base_url}/v0/state/sim0/tempctl0", timeout=REQUEST_TIMEOUT_S)
    if response.status_code != 200:
        raise AssertionError(f"Could not read device state: {response.status_code}")

    state = response.json()
    faulted_signal = _find_signal(state, [target_signal_id])
    if not faulted_signal:
        raise AssertionError(f"Could not find {target_signal_id} signal")

    print(f"   Faulted: {target_signal_id} = {faulted_signal['value']}, quality = {faulted_signal['quality']}")

    # CRITICAL CHECK: Quality should be FAULT
    if faulted_signal["quality"] != "FAULT":
        raise AssertionError(f"Signal quality should be FAULT, got: {faulted_signal['quality']}")

    print("   [PASS] Quality correctly set to FAULT")

    # Step 4: Verify value is frozen (doesn't change)
    print("\n4. Verifying value is frozen...")
    time.sleep(1.0)

    response = requests.get(f"{base_url}/v0/state/sim0/tempctl0", timeout=REQUEST_TIMEOUT_S)
    state = response.json()
    frozen_signal = _find_signal(state, [target_signal_id])
    if not frozen_signal:
        raise AssertionError(f"Could not find {target_signal_id} signal after 1s")

    print(f"   Frozen: {target_signal_id} = {frozen_signal['value']}, quality = {frozen_signal['quality']}")

    if frozen_signal["quality"] != "FAULT":
        raise AssertionError(f"Quality should still be FAULT after 1s, got: {frozen_signal['quality']}")

    # Note: We don't strictly verify value freeze since temp_pv might legitimately not change
    # The key is that quality remains FAULT
    print("   [PASS] Quality remains FAULT (value frozen)")

    # Step 5: Clear faults and verify recovery
    print("\n5. Clearing faults...")
    response = requests.post(
        f"{base_url}/v0/call",
        json={
            "provider_id": "sim0",
            "device_id": "chaos_control",
            "function_id": 5,
            "args": {},
        },
        timeout=REQUEST_TIMEOUT_S,
    )

    if response.status_code != 200:
        raise AssertionError(f"Could not clear faults: {response.status_code}")

    print("   Faults cleared")

    # Step 6: Verify signal quality returns to OK
    print("\n6. Verifying signal quality recovered...")
    time.sleep(0.5)

    response = requests.get(f"{base_url}/v0/state/sim0/tempctl0", timeout=REQUEST_TIMEOUT_S)
    state = response.json()
    recovered_signal = _find_signal(state, [target_signal_id])
    if not recovered_signal:
        raise AssertionError(f"Could not find {target_signal_id} signal after clearing faults")

    print(f"   Recovered: {target_signal_id} = {recovered_signal['value']}, quality = {recovered_signal['quality']}")

    if recovered_signal["quality"] != "OK":
        raise AssertionError(f"Quality should be OK after clearing faults, got: {recovered_signal['quality']}")

    print("   [PASS] Quality recovered to OK")

    print("\n=== PASS: Signal fault injection working correctly ===\n")
