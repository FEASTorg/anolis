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

from tests.support.api_helpers import wait_for_condition

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

    # Step 1: Read baseline signal value
    response = requests.get(f"{base_url}/v0/state/sim0/tempctl0", timeout=REQUEST_TIMEOUT_S)
    if response.status_code != 200:
        raise AssertionError(f"Could not read device state: {response.status_code}")

    state = response.json()
    # Prefer temp_pv where available (older model path), fall back to tc1_temp.
    target_signal_candidates = ["temp_pv", "tc1_temp"]
    baseline_signal = _find_signal(state, target_signal_candidates)
    if not baseline_signal:
        signal_ids = [s.get("signal_id", "<missing>") for s in state.get("values", [])]
        raise AssertionError(f"Could not find expected temperature signal. Available signals: {signal_ids}")
    target_signal_id = baseline_signal["signal_id"]

    # Step 2: Inject signal fault
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

    # Step 3: Wait for FAULT quality to propagate through polling/cache layers.
    latest_quality: str | None = None

    def fault_visible() -> bool:
        nonlocal latest_quality
        resp = requests.get(f"{base_url}/v0/state/sim0/tempctl0", timeout=REQUEST_TIMEOUT_S)
        if resp.status_code != 200:
            return False
        current_state = resp.json()
        signal = _find_signal(current_state, [target_signal_id])
        if not signal:
            return False
        latest_quality = cast(str, signal.get("quality"))
        return latest_quality == "FAULT"

    if not wait_for_condition(fault_visible, timeout=3.0, interval=0.1, description="signal quality FAULT"):
        raise AssertionError(f"Signal quality should become FAULT, got: {latest_quality}")

    # Step 4: Verify quality remains FAULT while fault is active
    time.sleep(1.0)

    response = requests.get(f"{base_url}/v0/state/sim0/tempctl0", timeout=REQUEST_TIMEOUT_S)
    state = response.json()
    frozen_signal = _find_signal(state, [target_signal_id])
    if not frozen_signal:
        raise AssertionError(f"Could not find {target_signal_id} signal after 1s")

    if frozen_signal["quality"] != "FAULT":
        raise AssertionError(f"Quality should still be FAULT after 1s, got: {frozen_signal['quality']}")

    # Step 5: Clear faults and verify recovery
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

    # Step 6: Wait for quality to recover to OK.
    latest_quality = None

    def quality_recovered() -> bool:
        nonlocal latest_quality
        resp = requests.get(f"{base_url}/v0/state/sim0/tempctl0", timeout=REQUEST_TIMEOUT_S)
        if resp.status_code != 200:
            return False
        current_state = resp.json()
        signal = _find_signal(current_state, [target_signal_id])
        if not signal:
            return False
        latest_quality = cast(str, signal.get("quality"))
        return latest_quality == "OK"

    if not wait_for_condition(quality_recovered, timeout=3.0, interval=0.1, description="signal quality OK"):
        raise AssertionError(f"Quality should recover to OK after clearing faults, got: {latest_quality}")
