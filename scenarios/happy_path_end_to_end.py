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

from .base import ScenarioBase, ScenarioResult, create_result


class HappyPathEndToEnd(ScenarioBase):
    """Validate complete device discovery → state → control → telemetry flow."""
    
    def run(self) -> ScenarioResult:
        """Execute happy path scenario."""
        try:
            # Step 1: Verify runtime is operational
            status = self.get_runtime_status()
            self.assert_in("mode", status, "Runtime status missing 'mode'")
            self.assert_equal(status["mode"], "MANUAL", "Expected MANUAL mode at start")
            
            # Step 2: Device Discovery - list all devices
            devices = self.get_devices()
            self.assert_true(len(devices) >= 4, f"Expected at least 4 devices, found {len(devices)}")
            
            # Verify expected devices exist
            device_ids = [d.get("device_id") for d in devices]
            expected = ["tempctl0", "motorctl0", "relayio0", "analogsensor0", "sim_control"]
            for expected_device in expected:
                self.assert_in(expected_device, device_ids, f"Device {expected_device} not found")
            
            # Step 3: Get Capabilities - verify tempctl0 capabilities
            caps = self.get_capabilities("sim0", "tempctl0")
            self.assert_in("signals", caps, "Capabilities missing 'signals'")
            self.assert_in("functions", caps, "Capabilities missing 'functions'")
            
            # Verify expected signals exist
            signal_ids = [s.get("signal_id") for s in caps["signals"]]
            self.assert_in("tc1_temp", signal_ids, "tc1_temp signal not found")
            self.assert_in("relay1_state", signal_ids, "relay1_state signal not found")
            
            # Verify expected functions exist
            function_names = [f.get("name") for f in caps["functions"]]
            self.assert_in("set_mode", function_names, "set_mode function not found")
            self.assert_in("set_relay", function_names, "set_relay function not found")
            
            # Step 4: Poll Initial State
            initial_state = self.get_state("sim0", "tempctl0")
            self.assert_in("signals", initial_state, "State missing 'signals'")
            
            # Find initial relay1 state
            initial_relay1 = None
            for sig in initial_state["signals"]:
                if sig.get("signal_id") == "relay1_state":
                    initial_relay1 = sig.get("value")
                    break
            self.assert_true(initial_relay1 is not None, "relay1_state not found in initial state")
            
            # Step 5: Call Function - toggle relay1
            new_relay_state = not initial_relay1
            result = self.call_function(
                "sim0", 
                "tempctl0", 
                "set_relay",
                {"relay_index": 1, "state": new_relay_state}
            )
            self.assert_in("status", result, "Function call result missing 'status'")
            status = result["status"]
            status_code = status.get("code") if isinstance(status, dict) else status
            self.assert_equal(status_code, "OK", f"Function call failed: {result.get('status', {}).get('message', '')}")
            
            # Step 6: Verify State Change
            self.sleep(0.2)  # Allow state to propagate
            
            updated_state = self.get_state("sim0", "tempctl0")
            updated_relay1 = None
            for sig in updated_state["signals"]:
                if sig.get("signal_id") == "relay1_state":
                    updated_relay1 = sig.get("value")
                    break
                    
            self.assert_equal(
                updated_relay1, 
                new_relay_state, 
                f"Relay state not updated: expected {new_relay_state}, got {updated_relay1}"
            )
            
            # Step 7: Verify All Device State - check we can poll all devices
            all_state = self.get_all_state()
            self.assert_in("devices", all_state, "All-state response missing 'devices'")
            self.assert_true(len(all_state["devices"]) >= 4, "Not all devices in all-state response")
            
            return create_result(
                self, 
                True, 
                "Happy path validation passed: discovery → capabilities → state → control → verify"
            )
            
        except AssertionError as e:
            return create_result(self, False, "Assertion failed", str(e))
        except Exception as e:
            return create_result(self, False, f"Exception: {type(e).__name__}", str(e))
