"""
Scenario: Precondition Enforcement

Validates that function calls are blocked when preconditions are not met.

Tests:
1. Verify tempctl0 set_relay blocked when mode is "closed"
2. Verify analogsensor0 calibrate_channel blocked when quality is not "GOOD"
3. Verify preconditions are checked before execution
"""

from .base import ScenarioBase


class PreconditionEnforcement(ScenarioBase):
    """Verify calls blocked when preconditions not met."""

    def run(self) -> None:
        """Execute precondition enforcement scenario."""
        # Test 1: tempctl0 - set_relay blocked in closed-loop mode

        # Step 1.1: Set tempctl0 to closed-loop mode
        result = self.call_function("sim0", "tempctl0", "set_mode", {"mode": "closed"})
        assert result["status"] == "OK", "Failed to set mode to closed"

        self.sleep(0.1)  # Allow state to update

        # Step 1.2: Verify mode is closed
        state = self.get_state("sim0", "tempctl0")
        mode_signal = None
        for sig in state["signals"]:
            if sig.get("signal_id") == "control_mode":
                mode_signal = sig.get("value")
                break
        assert mode_signal == "closed", "Mode not set to closed"

        # Step 1.3: Attempt to call set_relay - should fail precondition
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})

        # Should return FAILED_PRECONDITION status
        assert result.get("status") == "FAILED_PRECONDITION", (
            f"Expected FAILED_PRECONDITION, got {result.get('status')}"
        )

        # Step 1.4: Set back to open mode
        result = self.call_function("sim0", "tempctl0", "set_mode", {"mode": "open"})
        assert result["status"] == "OK", "Failed to set mode back to open"

        self.sleep(0.1)

        # Step 1.5: Now set_relay should succeed
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": False})
        assert result["status"] == "OK", "set_relay should succeed in open mode"

        # Test 2: analogsensor0 - calibrate_channel blocked when quality != "GOOD"
        def quality_value() -> str | None:
            state = self.get_state("sim0", "analogsensor0")
            for sig in state["signals"]:
                if sig.get("signal_id") == "sensor_quality":
                    value = sig.get("value")
                    return value if isinstance(value, str) else None
            return None

        # Establish known baseline quality.
        result = self.call_function("sim0", "analogsensor0", "inject_noise", {"enabled": False})
        assert result["status"] == "OK", "Failed to disable noise before calibration checks"

        try:
            good_ready = self.poll_until(lambda: quality_value() == "GOOD", timeout=5.0, interval=0.2)
            assert good_ready, f"Expected sensor_quality to settle at GOOD, got {quality_value()}"

            # Quality GOOD -> calibration allowed.
            result = self.call_function("sim0", "analogsensor0", "calibrate_channel", {"channel": 1})
            assert result["status"] == "OK", "calibrate_channel should succeed when quality is GOOD"

            # Enable noise and wait for degraded quality.
            result = self.call_function("sim0", "analogsensor0", "inject_noise", {"enabled": True})
            assert result["status"] == "OK", "Failed to enable noise injection"

            degraded_ready = self.poll_until(
                lambda: quality_value() in {"NOISY", "FAULT"},
                timeout=35.0,
                interval=0.5,
            )
            assert degraded_ready, f"Expected sensor_quality to degrade to NOISY/FAULT, got {quality_value()}"

            # Non-GOOD quality -> calibration blocked.
            result = self.call_function("sim0", "analogsensor0", "calibrate_channel", {"channel": 1})
            assert result.get("status") == "FAILED_PRECONDITION", (
                f"Expected FAILED_PRECONDITION when quality={quality_value()}, got {result.get('status')}"
            )
        finally:
            cleanup = self.call_function("sim0", "analogsensor0", "inject_noise", {"enabled": False})
            assert cleanup["status"] == "OK", "Failed to disable noise during cleanup"
