"""
Scenario: Parameter Validation

Validates that function parameters are validated before execution.

Tests:
1. Invalid parameter types rejected
2. Out-of-range parameter values rejected
3. Missing required parameters rejected
4. Valid parameters accepted
"""

from .base import ScenarioBase


class ParameterValidation(ScenarioBase):
    """Verify parameter updates respect type/range constraints."""

    def run(self) -> None:
        """Execute parameter validation scenario."""

        def assert_invalid_argument(result: dict, context: str) -> None:
            status = result.get("status")
            assert status == "INVALID_ARGUMENT", f"{context}: expected INVALID_ARGUMENT, got {status}"

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
        assert_invalid_argument(result, "Invalid relay_index type")

        # Test 2: Out-of-range parameter value - relay_index must be 1 or 2

        result = self.call_function(
            "sim0",
            "tempctl0",
            "set_relay",
            {"relay_index": 99, "state": True},  # Invalid: out of range
        )

        # Should fail with invalid argument
        assert_invalid_argument(result, "Out-of-range relay_index")

        # Test 3: Out-of-range parameter - setpoint temperature
        # tempctl0 setpoint range is -50 to 400 C

        result = self.call_function(
            "sim0",
            "tempctl0",
            "set_setpoint",
            {"value": 999.0},  # Invalid: > 400
        )

        # Should fail with invalid argument
        assert_invalid_argument(result, "Out-of-range setpoint")

        # Test 4: Missing required parameter

        result = self.call_function(
            "sim0",
            "tempctl0",
            "set_relay",
            {"relay_index": 1},  # Missing 'state' parameter
        )

        # Should fail with missing argument error
        assert_invalid_argument(result, "Missing required argument 'state'")

        # Test 5: Valid parameters - should succeed

        # Valid relay call
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})
        assert result["status"] == "OK", "Valid relay call should succeed"

        # Valid setpoint within range
        result = self.call_function("sim0", "tempctl0", "set_setpoint", {"value": 60.0})
        assert result["status"] == "OK", "Valid setpoint call should succeed"

        # Verify setpoint was actually set
        self.sleep(0.1)
        state = self.get_state("sim0", "tempctl0")
        setpoint_signal = None
        for sig in state["signals"]:
            if sig.get("signal_id") == "setpoint":
                setpoint_signal = sig.get("value")
                break

        assert setpoint_signal is not None and abs(setpoint_signal - 60.0) < 0.1, (
            f"Setpoint not updated correctly: expected 60.0, got {setpoint_signal}"
        )

        # Test 6: Boolean parameter validation

        result = self.call_function("sim0", "relayio0", "set_relay_ch1", {"enabled": True})
        assert result["status"] == "OK", "Valid boolean parameter should succeed"

        result = self.call_function("sim0", "relayio0", "set_relay_ch1", {"enabled": False})
        assert result["status"] == "OK", "Valid boolean parameter should succeed"
