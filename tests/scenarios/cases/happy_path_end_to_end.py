"""
Scenario: Happy Path End-to-End

Validates the full device control flow:
1. Runtime starts with provider-sim
2. Devices are discovered
3. Device capabilities retrieved
4. Device state polled
5. Function called to change state
6. State change verified
7. Telemetry captured

This is the baseline "everything works" scenario.
"""

from .base import ScenarioBase


class HappyPathEndToEnd(ScenarioBase):
    """Validate complete device discovery -> state -> control -> telemetry flow."""

    def run(self) -> None:
        """Execute happy path scenario."""
        # Step 1: Verify runtime is operational
        status = self.get_runtime_status()
        assert "mode" in status, "Runtime status missing 'mode'"
        assert status["mode"] == "MANUAL", "Expected MANUAL mode at start"

        # Step 2: Device Discovery - list all devices
        devices = self.get_devices()
        assert len(devices) >= 4, f"Expected at least 4 devices, found {len(devices)}"

        # Verify expected devices exist
        device_ids = [d.get("device_id") for d in devices]
        expected = [
            "tempctl0",
            "motorctl0",
            "relayio0",
            "analogsensor0",
            "chaos_control",
        ]
        for expected_device in expected:
            assert expected_device in device_ids, f"Device {expected_device} not found"

        # Step 3: Get Capabilities - verify tempctl0 capabilities
        caps = self.get_capabilities("sim0", "tempctl0")
        assert "signals" in caps, "Capabilities missing 'signals'"
        assert "functions" in caps, "Capabilities missing 'functions'"

        # Verify expected signals exist
        signal_ids = [s.get("signal_id") for s in caps["signals"]]
        assert "tc1_temp" in signal_ids, "tc1_temp signal not found"
        assert "relay1_state" in signal_ids, "relay1_state signal not found"

        # Verify expected functions exist
        function_names = [f.get("name") for f in caps["functions"]]
        assert "set_mode" in function_names, "set_mode function not found"
        assert "set_relay" in function_names, "set_relay function not found"

        # Step 4: Poll Initial State
        initial_state = self.get_state("sim0", "tempctl0")
        assert "signals" in initial_state, "State missing 'signals'"

        # Find initial relay1 state
        initial_relay1 = None
        for sig in initial_state["signals"]:
            if sig.get("signal_id") == "relay1_state":
                initial_relay1 = sig.get("value")
                break
        assert initial_relay1 is not None, "relay1_state not found in initial state"

        # Step 5: Call Function - toggle relay1
        new_relay_state = not initial_relay1
        result = self.call_function(
            "sim0",
            "tempctl0",
            "set_relay",
            {"relay_index": 1, "state": new_relay_state},
        )
        assert "status" in result, "Function call result missing 'status'"
        assert result["status"] == "OK", f"Function call failed: {result}"

        # Step 6: Verify State Change
        self.sleep(0.2)  # Allow state to propagate

        updated_state = self.get_state("sim0", "tempctl0")
        updated_relay1 = None
        for sig in updated_state["signals"]:
            if sig.get("signal_id") == "relay1_state":
                updated_relay1 = sig.get("value")
                break

        assert updated_relay1 == new_relay_state, (
            f"Relay state not updated: expected {new_relay_state}, got {updated_relay1}"
        )

        # Step 7: Verify All Device State - check we can poll all devices
        all_state = self.get_all_state()
        assert "devices" in all_state, "All-state response missing 'devices'"
        assert len(all_state["devices"]) >= 4, "Not all devices in all-state response"
