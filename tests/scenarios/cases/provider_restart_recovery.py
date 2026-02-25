"""
Scenario: Provider Restart Recovery

Validates that runtime recovers gracefully when provider restarts.

Tests:
1. Verify provider and devices operational
2. Inject device unavailable fault to simulate provider failure
3. Verify runtime handles unavailability gracefully
4. Clear fault (simulating provider recovery)
5. Verify devices become accessible again
6. Verify runtime resumes normal operation

Note: Full provider restart testing would require process management.
This scenario uses fault injection to simulate provider unavailability and recovery.
"""

import requests

from .base import ScenarioBase


class ProviderRestartRecovery(ScenarioBase):
    """Runtime recovers gracefully when provider restarts."""

    def run(self) -> None:
        """Execute provider restart recovery scenario."""
        # Step 1: Verify all devices operational initially
        devices = self.get_devices()
        assert len(devices) >= 4, f"Expected at least 4 devices, found {len(devices)}"

        device_ids = [d.get("device_id") for d in devices]
        expected_devices = ["tempctl0", "motorctl0", "relayio0", "analogsensor0"]
        for expected in expected_devices:
            assert expected in device_ids, f"Device {expected} not found initially"

        # Step 2: Get baseline state from each device
        baseline_states = {}
        for device_id in expected_devices:
            state = self.get_state("sim0", device_id)
            signal_count = len(state.get("signals", []))
            baseline_states[device_id] = signal_count
            assert signal_count > 0, f"Device {device_id} returned no signals initially"

        # Step 3: Simulate provider failure by making all devices unavailable
        for device_id in expected_devices:
            result = self.call_function(
                "sim0",
                "chaos_control",
                "inject_device_unavailable",
                {"device_id": device_id, "duration_ms": 20000},
            )
            assert result["status"] == "OK", f"Failed to inject unavailable fault for {device_id}"

        # Step 4: Verify devices become unavailable
        self.sleep(1.5)

        unavailable_count = 0
        for device_id in expected_devices:
            try:
                state = self.get_state("sim0", device_id)
                if len(state.get("signals", [])) == 0:
                    unavailable_count += 1
            except (requests.RequestException, RuntimeError):
                unavailable_count += 1

        assert unavailable_count > 0, "No devices became unavailable after fault injection"

        # Step 5: Verify runtime remains responsive during provider outage
        status = self.get_runtime_status()
        assert "mode" in status, "Runtime status should still be accessible"

        # Step 6: Clear all faults (simulating provider recovery)
        result = self.call_function("sim0", "chaos_control", "clear_faults", {})
        assert result["status"] == "OK", "Failed to clear faults"

        # Step 7: Verify devices become accessible again
        recovered = self.poll_until(
            lambda: all(len(self.get_state("sim0", d).get("signals", [])) > 0 for d in expected_devices),
            timeout=5.0,
            interval=0.5,
        )
        if not recovered:
            unrecovered = []
            for device_id in expected_devices:
                try:
                    state = self.get_state("sim0", device_id)
                    if len(state.get("signals", [])) == 0:
                        unrecovered.append(device_id)
                except (requests.RequestException, RuntimeError):
                    unrecovered.append(device_id)
            raise AssertionError(f"Not all devices recovered within timeout: {unrecovered}")

        # Step 8: Verify device state is consistent after recovery
        for device_id in expected_devices:
            state = self.get_state("sim0", device_id)
            signal_count = len(state.get("signals", []))
            assert signal_count == baseline_states[device_id], (
                f"Device {device_id} signal count changed after recovery: "
                f"{baseline_states[device_id]} -> {signal_count}"
            )

        # Step 9: Verify function calls work after recovery
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})
        assert result["status"] == "OK", "Function calls should work after provider recovery"

        # Step 10: Verify state change took effect
        self.sleep(0.2)

        state = self.get_state("sim0", "tempctl0")
        relay1_state = None
        for sig in state["signals"]:
            if sig.get("signal_id") == "relay1_state":
                relay1_state = sig.get("value")
                break

        assert relay1_state is True, "State changes should work after provider recovery"

        # Step 11: Verify all device list is complete
        devices_after = self.get_devices()
        assert len(devices_after) == len(devices), (
            f"Device count changed after recovery: {len(devices)} -> {len(devices_after)}"
        )
