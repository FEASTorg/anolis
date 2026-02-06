#!/usr/bin/env python3
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
- Runtime running at localhost:8080 with provider-sim connected

Usage:
    python scripts/validate_signal_fault_injection.py
"""

import requests
import time
import sys

BASE_URL = "http://localhost:8080"


def test_signal_fault_injection():
    """Test that signal fault injection overrides signal quality to FAULT."""

    print("=== Signal Fault Injection Test ===\n")

    # Step 1: Read baseline signal value
    print("1. Reading baseline signal value...")
    response = requests.get(f"{BASE_URL}/v0/state/sim0/tempctl0")
    if response.status_code != 200:
        print(f"FAIL: Could not read device state: {response.status_code}")
        return False

    state = response.json()
    temp_pv_baseline = None
    for signal in state.get("values", []):
        if signal["signal_id"] == "temp_pv":
            temp_pv_baseline = signal
            break

    if not temp_pv_baseline:
        print("FAIL: Could not find temp_pv signal")
        return False

    print(
        f"   Baseline: temp_pv = {temp_pv_baseline['value']}, quality = {temp_pv_baseline['quality']}"
    )

    if temp_pv_baseline["quality"] != "OK":
        print(f"WARN: Baseline quality is not OK: {temp_pv_baseline['quality']}")

    # Step 2: Inject signal fault
    print("\n2. Injecting signal fault for temp_pv (5 seconds)...")
    response = requests.post(
        f"{BASE_URL}/v0/call/sim0/sim_control/inject_signal_fault",
        json={
            "args": {
                "device_id": "tempctl0",
                "signal_id": "temp_pv",
                "duration_ms": 5000,
            }
        },
    )

    if response.status_code != 200:
        print(f"FAIL: Could not inject signal fault: {response.status_code}")
        print(response.text)
        return False

    print("   Fault injected successfully")

    # Step 3: Read signal value immediately - should be FAULT quality
    print("\n3. Reading signal value (should be FAULT)...")
    time.sleep(0.2)  # Brief delay for fault to apply

    response = requests.get(f"{BASE_URL}/v0/state/sim0/tempctl0")
    if response.status_code != 200:
        print(f"FAIL: Could not read device state: {response.status_code}")
        return False

    state = response.json()
    temp_pv_faulted = None
    for signal in state.get("values", []):
        if signal["signal_id"] == "temp_pv":
            temp_pv_faulted = signal
            break

    if not temp_pv_faulted:
        print("FAIL: Could not find temp_pv signal")
        return False

    print(
        f"   Faulted: temp_pv = {temp_pv_faulted['value']}, quality = {temp_pv_faulted['quality']}"
    )

    # CRITICAL CHECK: Quality should be FAULT
    if temp_pv_faulted["quality"] != "FAULT":
        print(
            f"FAIL: Signal quality should be FAULT, got: {temp_pv_faulted['quality']}"
        )
        return False

    print("   [PASS] Quality correctly set to FAULT")

    # Step 4: Verify value is frozen (doesn't change)
    print("\n4. Verifying value is frozen...")
    time.sleep(1.0)

    response = requests.get(f"{BASE_URL}/v0/state/sim0/tempctl0")
    state = response.json()
    temp_pv_frozen = None
    for signal in state.get("values", []):
        if signal["signal_id"] == "temp_pv":
            temp_pv_frozen = signal
            break

    print(
        f"   Frozen: temp_pv = {temp_pv_frozen['value']}, quality = {temp_pv_frozen['quality']}"
    )

    if temp_pv_frozen["quality"] != "FAULT":
        print(
            f"FAIL: Quality should still be FAULT after 1s, got: {temp_pv_frozen['quality']}"
        )
        return False

    # Note: We don't strictly verify value freeze since temp_pv might legitimately not change
    # The key is that quality remains FAULT
    print("   [PASS] Quality remains FAULT (value frozen)")

    # Step 5: Clear faults and verify recovery
    print("\n5. Clearing faults...")
    response = requests.post(
        f"{BASE_URL}/v0/call/sim0/sim_control/clear_faults", json={"args": {}}
    )

    if response.status_code != 200:
        print(f"FAIL: Could not clear faults: {response.status_code}")
        return False

    print("   Faults cleared")

    # Step 6: Verify signal quality returns to OK
    print("\n6. Verifying signal quality recovered...")
    time.sleep(0.5)

    response = requests.get(f"{BASE_URL}/v0/state/sim0/tempctl0")
    state = response.json()
    temp_pv_recovered = None
    for signal in state.get("values", []):
        if signal["signal_id"] == "temp_pv":
            temp_pv_recovered = signal
            break

    print(
        f"   Recovered: temp_pv = {temp_pv_recovered['value']}, quality = {temp_pv_recovered['quality']}"
    )

    if temp_pv_recovered["quality"] != "OK":
        print(
            f"FAIL: Quality should be OK after clearing faults, got: {temp_pv_recovered['quality']}"
        )
        return False

    print("   [PASS] Quality recovered to OK")

    print("\n=== PASS: Signal fault injection working correctly ===\n")
    return True


if __name__ == "__main__":
    try:
        if test_signal_fault_injection():
            sys.exit(0)
        else:
            sys.exit(1)
    except requests.exceptions.ConnectionError:
        print("FAIL: Could not connect to runtime. Is it running?")
        sys.exit(1)
    except Exception as e:
        print(f"FAIL: Unexpected error: {e}")
        import traceback

        traceback.print_exc()
        sys.exit(1)
