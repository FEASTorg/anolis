"""
Test Scenario: Infrastructure Validation

Simple scenario to verify scenario infrastructure is working.
This will be removed once real scenarios are implemented.
"""

from .base import ScenarioBase, ScenarioResult, create_result


class TestInfrastructure(ScenarioBase):
    """Verify scenario infrastructure is functioning."""
    
    def run(self) -> ScenarioResult:
        """Run basic infrastructure validation."""
        try:
            # Test 1: Can we list devices?
            devices = self.get_devices()
            self.assert_true(len(devices) > 0, "No devices found")
            
            # Test 2: Can we find sim_control device?
            found_control = False
            for d in devices:
                if d.get("device_id") == "sim_control":
                    found_control = True
                    break
            self.assert_true(found_control, "sim_control device not found")
            
            # Test 3: Can we get device state?
            state = self.get_state("sim0", "tempctl0")
            self.assert_in("values", state, "Device state missing 'values' field")
            
            # Test 4: Can we call clear_faults?
            result = self.call_function("sim0", "sim_control", 5, {})
            self.assert_equal(result.get("status", {}).get("code"), "OK", "clear_faults call failed")
            
            return create_result(self, True, "Infrastructure validation passed")
            
        except AssertionError as e:
            return create_result(self, False, "Assertion failed", str(e))
        except Exception as e:
            return create_result(self, False, f"Exception: {type(e).__name__}", str(e))
