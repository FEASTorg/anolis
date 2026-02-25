"""
Scenario: Parameter Validation

Validates that function parameters are validated before execution.

Tests:
1. Invalid parameter types rejected
2. Out-of-range parameter values rejected
3. Missing required parameters rejected
4. Valid parameters accepted
"""

from .base import ScenarioBase, ScenarioResult, create_result


class ParameterValidation(ScenarioBase):
    """Verify parameter updates respect type/range constraints."""

    def run(self) -> ScenarioResult:
        """Execute parameter validation scenario."""
        try:
            # Test 1: Invalid parameter type - pass string where int expected

            result = self.call_function(
                "sim0",
                "tempctl0",
                "set_relay",
                {
                    "relay_index": "not_a_number",
                    "state": True,
                },  # Invalid: string instead of int
            )

            # Should fail with invalid argument error
            self.assert_true(
                result.get("status") in ["INVALID_ARGUMENT", "BAD_REQUEST", "ERROR"],
                f"Expected parameter type error, got {result.get('status')}",
            )

            # Test 2: Out-of-range parameter value - relay_index must be 1 or 2

            result = self.call_function(
                "sim0",
                "tempctl0",
                "set_relay",
                {"relay_index": 99, "state": True},  # Invalid: out of range
            )

            # Should fail with invalid argument or out of range error
            self.assert_true(
                result.get("status") in ["INVALID_ARGUMENT", "BAD_REQUEST", "ERROR", "OUT_OF_RANGE"],
                f"Expected out-of-range error, got {result.get('status')}",
            )

            # Test 3: Out-of-range parameter - setpoint temperature
            # tempctl0 setpoint range is -50 to 400 C

            result = self.call_function(
                "sim0",
                "tempctl0",
                "set_setpoint",
                {"value": 999.0},  # Invalid: > 400
            )

            # Should fail
            self.assert_true(
                result.get("status") in ["INVALID_ARGUMENT", "BAD_REQUEST", "ERROR", "OUT_OF_RANGE"],
                f"Expected out-of-range error for setpoint, got {result.get('status')}",
            )

            # Test 4: Missing required parameter

            result = self.call_function(
                "sim0",
                "tempctl0",
                "set_relay",
                {"relay_index": 1},  # Missing 'state' parameter
            )

            # Should fail with missing argument error
            self.assert_true(
                result.get("status") in ["INVALID_ARGUMENT", "BAD_REQUEST", "ERROR"],
                f"Expected missing parameter error, got {result.get('status')}",
            )

            # Test 5: Valid parameters - should succeed

            # Valid relay call
            result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})
            self.assert_equal(result["status"], "OK", "Valid relay call should succeed")

            # Valid setpoint within range
            result = self.call_function("sim0", "tempctl0", "set_setpoint", {"value": 60.0})
            self.assert_equal(result["status"], "OK", "Valid setpoint call should succeed")

            # Verify setpoint was actually set
            self.sleep(0.1)
            state = self.get_state("sim0", "tempctl0")
            setpoint_signal = None
            for sig in state["signals"]:
                if sig.get("signal_id") == "setpoint":
                    setpoint_signal = sig.get("value")
                    break

            self.assert_true(
                setpoint_signal is not None and abs(setpoint_signal - 60.0) < 0.1,
                f"Setpoint not updated correctly: expected 60.0, got {setpoint_signal}",
            )

            # Test 6: Boolean parameter validation

            result = self.call_function("sim0", "relayio0", "set_relay_ch1", {"enabled": True})
            self.assert_equal(result["status"], "OK", "Valid boolean parameter should succeed")

            result = self.call_function("sim0", "relayio0", "set_relay_ch1", {"enabled": False})
            self.assert_equal(result["status"], "OK", "Valid boolean parameter should succeed")

            return create_result(
                self,
                True,
                "Parameter validation enforced: invalid types/ranges rejected, valid parameters accepted",
            )

        except AssertionError as e:
            return create_result(self, False, "Assertion failed", str(e))
        except Exception as e:
            return create_result(self, False, f"Exception: {type(e).__name__}", str(e))
