"""
Scenario: Mode Blocking Policy

Validates that BLOCK policy prevents manual function calls when in AUTO mode.

Tests:
1. Start in MANUAL mode
2. Call function - should succeed
3. Switch to AUTO mode
4. Attempt function call with BLOCK policy - should fail
5. Switch back to MANUAL - function calls work again
"""

from .base import ScenarioBase


class ModeBlockingPolicy(ScenarioBase):
    """Verify AUTO mode blocks manual calls when policy is BLOCK."""

    def run(self) -> None:
        """Execute mode blocking policy scenario."""
        # Step 1: Verify we're in MANUAL mode
        self.assert_mode("MANUAL")

        # Step 2: Manual function call in MANUAL mode - should succeed
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})
        assert result["status"] == "OK", "Function call should succeed in MANUAL mode"

        # Step 3: Switch to AUTO mode
        self.set_mode("AUTO")
        assert self.wait_for_mode("AUTO", timeout=2.0), "Failed to switch to AUTO mode"

        # Step 4: Attempt manual function call in AUTO mode - should be blocked
        # The runtime should return an error indicating the call is blocked
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": False})
        assert result.get("status") == "FAILED_PRECONDITION", (
            f"Expected FAILED_PRECONDITION in AUTO mode, got: {result.get('status')}"
        )

        # Step 5: Switch back to MANUAL mode
        self.set_mode("MANUAL")
        assert self.wait_for_mode("MANUAL", timeout=2.0), "Failed to switch back to MANUAL mode"

        # Step 6: Verify function calls work again in MANUAL mode
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})
        assert result["status"] == "OK", "Function call should succeed in MANUAL mode"
