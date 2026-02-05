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

from .base import ScenarioBase, ScenarioResult, create_result


class ProviderRestartRecovery(ScenarioBase):
    """Runtime recovers gracefully when provider restarts."""
    
    def run(self) -> ScenarioResult:
        """Execute provider restart recovery scenario."""
        try:
            # Step 1: Verify all devices operational initially
            devices = self.get_devices()
            self.assert_true(len(devices) >= 4, f"Expected at least 4 devices, found {len(devices)}")
            
            device_ids = [d.get("device_id") for d in devices]
            expected_devices = ["tempctl0", "motorctl0", "relayio0", "analogsensor0"]
            for expected in expected_devices:
                self.assert_in(expected, device_ids, f"Device {expected} not found initially")
            
            # Step 2: Get baseline state from each device
            baseline_states = {}
            for device_id in expected_devices:
                state = self.get_state("sim0", device_id)
                baseline_states[device_id] = len(state.get("signals", []))
                self.assert_true(
                    baseline_states[device_id] > 0,
                    f"Device {device_id} returned no signals initially"
                )
            
            # Step 3: Simulate provider failure by making all devices unavailable
            # Inject unavailable fault for all devices simultaneously
            for device_id in expected_devices:
                result = self.call_function(
                    "sim0",
                    "sim_control",
                    "inject_device_unavailable",
                    {"device_id": device_id, "duration_ms": 5000}
                )
                self.assert_equal(
                    result["status"],
                    "OK",
                    f"Failed to inject unavailable fault for {device_id}"
                )
            
            # Step 4: Verify devices become unavailable
            # Wait enough time for polling cycle (default 500ms) to detect failure
            self.sleep(1.5)
            
            unavailable_count = 0
            for device_id in expected_devices:
                try:
                    state = self.get_state("sim0", device_id)
                    signals = state.get("signals", [])
                    print(f"[DEBUG] Device {device_id} has {len(signals)} signals")
                    if len(signals) == 0:
                        unavailable_count += 1
                except Exception as e:
                    print(f"[DEBUG] Device {device_id} poll exception: {e}")
                    # Expected - device unavailable
                    unavailable_count += 1
            
            # At least some devices should be unavailable
            self.assert_true(
                unavailable_count > 0,
                "No devices became unavailable after fault injection"
            )
            
            # Step 5: Verify runtime remains responsive during provider outage
            # Runtime status should still be accessible
            status = self.get_runtime_status()
            self.assert_in("mode", status, "Runtime status should still be accessible")
            
            # Step 6: Clear all faults (simulating provider recovery)
            result = self.call_function(
                "sim0",
                "sim_control",
                "clear_faults",
                {}
            )
            self.assert_equal(result["status"], "OK", "Failed to clear faults")
            
            # Step 7: Verify devices become accessible again
            self.sleep(0.3)
            
            recovered_devices = 0
            for device_id in expected_devices:
                try:
                    state = self.get_state("sim0", device_id)
                    signals = state.get("signals", [])
                    if len(signals) > 0:
                        recovered_devices += 1
                except Exception as e:
                    # Device still unavailable
                    pass
            
            self.assert_equal(
                recovered_devices,
                len(expected_devices),
                f"Not all devices recovered: {recovered_devices}/{len(expected_devices)}"
            )
            
            # Step 8: Verify device state is consistent after recovery
            for device_id in expected_devices:
                state = self.get_state("sim0", device_id)
                signal_count = len(state.get("signals", []))
                self.assert_equal(
                    signal_count,
                    baseline_states[device_id],
                    f"Device {device_id} signal count changed after recovery: "
                    f"{baseline_states[device_id]} -> {signal_count}"
                )
            
            # Step 9: Verify function calls work after recovery
            result = self.call_function(
                "sim0",
                "tempctl0",
                "set_relay",
                {"relay_index": 1, "state": True}
            )
            self.assert_equal(
                result["status"],
                "OK",
                "Function calls should work after provider recovery"
            )
            
            # Step 10: Verify state change took effect
            self.sleep(0.2)
            
            state = self.get_state("sim0", "tempctl0")
            relay1_state = None
            for sig in state["signals"]:
                if sig.get("signal_id") == "relay1_state":
                    relay1_state = sig.get("value")
                    break
            
            self.assert_equal(
                relay1_state,
                True,
                "State changes should work after provider recovery"
            )
            
            # Step 11: Verify all device list is complete
            devices_after = self.get_devices()
            self.assert_equal(
                len(devices_after),
                len(devices),
                f"Device count changed after recovery: {len(devices)} -> {len(devices_after)}"
            )
            
            return create_result(
                self,
                True,
                f"Provider restart recovery validated: {len(expected_devices)} devices recovered, "
                f"runtime resumed normal operation"
            )
            
        except AssertionError as e:
            return create_result(self, False, "Assertion failed", str(e))
        except Exception as e:
            return create_result(self, False, f"Exception: {type(e).__name__}", str(e))
