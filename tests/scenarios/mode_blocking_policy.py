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

from .base import ScenarioBase, ScenarioResult, create_result


class ModeBlockingPolicy(ScenarioBase):
    """Verify AUTO mode blocks manual calls when policy is BLOCK."""
    
    def run(self) -> ScenarioResult:
        """Execute mode blocking policy scenario."""
        try:
            # Step 1: Verify we're in MANUAL mode
            self.assert_mode("MANUAL")
            
            # Step 2: Manual function call in MANUAL mode - should succeed
            result = self.call_function(
                "sim0",
                "tempctl0",
                "set_relay",
                {"relay_index": 1, "state": True}
            )
            self.assert_equal(result["status"], "OK", "Function call should succeed in MANUAL mode")
            
            # Step 3: Switch to AUTO mode
            self.set_mode("AUTO")
            self.assert_true(
                self.wait_for_mode("AUTO", timeout=2.0),
                "Failed to switch to AUTO mode"
            )
            
            # Step 4: Attempt manual function call in AUTO mode - should be blocked
            # The runtime should return an error indicating the call is blocked
            try:
                result = self.call_function(
                    "sim0",
                    "tempctl0",
                    "set_relay",
                    {"relay_index": 1, "state": False}
                )
                
                # If call succeeded, check if it was actually blocked by examining status
                # In AUTO mode with BLOCK policy, the call should either:
                # - Return HTTP error (403/409), or
                # - Return status != "OK"
                if result.get("status") == "OK":
                    return create_result(
                        self,
                        False,
                        "Mode blocking failed",
                        "Manual function call succeeded in AUTO mode when it should be blocked"
                    )
                    
            except Exception as e:
                # If we get an HTTP error, that's expected for blocking
                # This is the correct behavior
                import requests
                if isinstance(e, requests.HTTPError):
                    # Expected - call was blocked
                    pass
                else:
                    raise  # Unexpected error type
            
            # Step 5: Switch back to MANUAL mode
            self.set_mode("MANUAL")
            self.assert_true(
                self.wait_for_mode("MANUAL", timeout=2.0),
                "Failed to switch back to MANUAL mode"
            )
            
            # Step 6: Verify function calls work again in MANUAL mode
            result = self.call_function(
                "sim0",
                "tempctl0",
                "set_relay",
                {"relay_index": 1, "state": True}
            )
            self.assert_equal(result["status"], "OK", "Function call should succeed in MANUAL mode")
            
            return create_result(
                self,
                True,
                "Mode blocking policy validated: AUTO mode blocks manual calls, MANUAL allows them"
            )
            
        except AssertionError as e:
            return create_result(self, False, "Assertion failed", str(e))
        except Exception as e:
            return create_result(self, False, f"Exception: {type(e).__name__}", str(e))
