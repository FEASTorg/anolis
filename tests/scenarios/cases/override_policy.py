"""
Scenario: Override Policy

Validates that OVERRIDE policy allows manual calls while runtime remains in AUTO mode.

Tests:
1. Start in MANUAL mode
2. Switch to AUTO mode
3. Manual function call with OVERRIDE policy - should succeed
4. Verify function call was executed
5. Verify runtime mode remains AUTO
"""

from .base import ScenarioBase


class OverridePolicy(ScenarioBase):
    """Verify OVERRIDE policy permits manual calls in AUTO mode."""

    def run(self) -> None:
        """Execute override policy scenario."""
        # Step 1: Verify we're in MANUAL mode
        self.assert_mode("MANUAL")

        # Step 2: Switch to AUTO mode
        self.set_mode("AUTO")
        assert self.wait_for_mode("AUTO", timeout=2.0), "Failed to switch to AUTO mode"

        # Step 3: Make manual function call in AUTO mode.
        # With OVERRIDE policy this should succeed (not blocked).

        # Get initial relay state
        initial_state = self.get_state("sim0", "tempctl0")
        initial_relay1 = None
        for sig in initial_state["signals"]:
            if sig.get("signal_id") == "relay1_state":
                initial_relay1 = sig.get("value")
                break

        # Call function to toggle relay
        new_relay_state = not initial_relay1
        result = self.call_function(
            "sim0",
            "tempctl0",
            "set_relay",
            {"relay_index": 1, "state": new_relay_state},
        )

        # Step 4: Verify call succeeded
        assert result.get("status") == "OK", f"Override call should succeed in AUTO mode: {result.get('message', '')}"

        # Step 5: Verify state changed (override was applied)
        self.sleep(0.2)  # Allow state to propagate

        updated_state = self.get_state("sim0", "tempctl0")
        updated_relay1 = None
        for sig in updated_state["signals"]:
            if sig.get("signal_id") == "relay1_state":
                updated_relay1 = sig.get("value")
                break

        assert updated_relay1 == new_relay_state, (
            f"Relay state not updated by override: expected {new_relay_state}, got {updated_relay1}"
        )

        # Step 6: Runtime remains in AUTO mode.
        status = self.get_runtime_status()
        current_mode = status.get("mode")
        assert current_mode == "AUTO", f"Expected AUTO mode after OVERRIDE call, got: {current_mode}"
