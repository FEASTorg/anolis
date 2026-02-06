"""
Scenario: Multi-Device Concurrency

Validates that multiple devices can be polled and controlled concurrently without deadlock.

Tests:
1. Verify all devices accessible
2. Poll all devices concurrently
3. Control all devices concurrently
4. Verify no deadlocks or race conditions
5. Verify all state changes applied correctly
"""

from .base import ScenarioBase, ScenarioResult, create_result
import threading
import time


class MultiDeviceConcurrency(ScenarioBase):
    """Multiple devices polled and controlled concurrently without deadlock."""

    def run(self) -> ScenarioResult:
        """Execute multi-device concurrency scenario."""
        try:
            # Step 1: Verify all devices are accessible
            devices = self.get_devices()
            device_ids = [d.get("device_id") for d in devices]

            expected_devices = ["tempctl0", "motorctl0", "relayio0", "analogsensor0"]
            for expected in expected_devices:
                self.assert_in(expected, device_ids, f"Device {expected} not found")

            # Step 2: Concurrent state polling from all devices
            poll_results = {}
            poll_errors = []

            def poll_device(device_id):
                try:
                    start = time.time()
                    state = self.get_state("sim0", device_id)
                    latency = time.time() - start
                    poll_results[device_id] = {
                        "success": True,
                        "latency": latency,
                        "signal_count": len(state.get("signals", [])),
                    }
                except Exception as e:
                    poll_errors.append((device_id, str(e)))

            # Launch concurrent polls
            threads = []
            for device_id in expected_devices:
                t = threading.Thread(target=poll_device, args=(device_id,))
                threads.append(t)
                t.start()

            # Wait for all polls to complete
            for t in threads:
                t.join(timeout=5.0)

            # Verify all polls succeeded
            self.assert_equal(
                len(poll_errors), 0, f"Concurrent polls failed: {poll_errors}"
            )

            self.assert_equal(
                len(poll_results),
                len(expected_devices),
                f"Not all devices polled: {len(poll_results)}/{len(expected_devices)}",
            )

            # Verify all devices returned signals
            for device_id, result in poll_results.items():
                self.assert_true(
                    result["signal_count"] > 0,
                    f"Device {device_id} returned no signals",
                )

            # Step 3: Concurrent function calls to multiple devices
            call_results = {}
            call_errors = []

            def call_device_function(device_id, function_name, args):
                try:
                    start = time.time()
                    result = self.call_function("sim0", device_id, function_name, args)
                    latency = time.time() - start
                    call_results[device_id] = {
                        "success": result.get("status") == "OK",
                        "latency": latency,
                        "status": result.get("status"),
                    }
                except Exception as e:
                    call_errors.append((device_id, function_name, str(e)))

            # Define concurrent function calls
            concurrent_calls = [
                ("tempctl0", "set_relay", {"relay_index": 1, "state": True}),
                ("motorctl0", "set_motor_duty", {"motor_index": 1, "duty": 0.5}),
                ("relayio0", "set_relay_ch1", {"enabled": True}),
                ("relayio0", "set_relay_ch2", {"enabled": False}),
            ]

            # Launch concurrent function calls
            threads = []
            for device_id, function_name, args in concurrent_calls:
                t = threading.Thread(
                    target=call_device_function, args=(device_id, function_name, args)
                )
                threads.append(t)
                t.start()

            # Wait for all calls to complete
            for t in threads:
                t.join(timeout=5.0)

            # Verify all calls succeeded
            self.assert_equal(
                len(call_errors), 0, f"Concurrent function calls failed: {call_errors}"
            )

            for device_id, result in call_results.items():
                self.assert_true(
                    result["success"],
                    f"Function call to {device_id} failed: {result['status']}",
                )

            # Step 4: Verify all state changes were applied
            self.sleep(0.3)  # Allow state to propagate

            # Check tempctl0 relay1 state
            state = self.get_state("sim0", "tempctl0")
            relay1_state = None
            for sig in state["signals"]:
                if sig.get("signal_id") == "relay1_state":
                    relay1_state = sig.get("value")
                    break
            self.assert_equal(relay1_state, True, "tempctl0 relay1 not updated")

            # Check motorctl0 duty
            state = self.get_state("sim0", "motorctl0")
            motor1_duty = None
            for sig in state["signals"]:
                if sig.get("signal_id") == "motor1_duty":
                    motor1_duty = sig.get("value")
                    break
            self.assert_true(
                abs(motor1_duty - 0.5) < 0.01,
                f"motorctl0 duty not updated: expected 0.5, got {motor1_duty}",
            )

            # Check relayio0 states
            state = self.get_state("sim0", "relayio0")
            relay_ch1 = None
            relay_ch2 = None
            for sig in state["signals"]:
                if sig.get("signal_id") == "relay_ch1_state":
                    relay_ch1 = sig.get("value")
                elif sig.get("signal_id") == "relay_ch2_state":
                    relay_ch2 = sig.get("value")
            self.assert_equal(relay_ch1, True, "relayio0 ch1 not updated")
            self.assert_equal(relay_ch2, False, "relayio0 ch2 not updated")

            # Step 5: Rapid sequential operations to stress test
            rapid_count = 20
            rapid_errors = []

            for i in range(rapid_count):
                try:
                    # Alternate between different devices
                    if i % 4 == 0:
                        self.get_state("sim0", "tempctl0")
                    elif i % 4 == 1:
                        self.get_state("sim0", "motorctl0")
                    elif i % 4 == 2:
                        self.call_function(
                            "sim0", "relayio0", "set_relay_ch1", {"enabled": i % 2 == 0}
                        )
                    else:
                        self.get_state("sim0", "analogsensor0")
                except Exception as e:
                    rapid_errors.append((i, str(e)))

            # Should have no errors under rapid sequential load
            self.assert_equal(
                len(rapid_errors),
                0,
                f"Rapid sequential operations failed: {rapid_errors}",
            )

            # Calculate average latencies
            avg_poll_latency = sum(r["latency"] for r in poll_results.values()) / len(
                poll_results
            )
            avg_call_latency = sum(r["latency"] for r in call_results.values()) / len(
                call_results
            )

            return create_result(
                self,
                True,
                f"Multi-device concurrency validated: all operations successful "
                f"(poll: {avg_poll_latency:.3f}s, call: {avg_call_latency:.3f}s)",
            )

        except AssertionError as e:
            return create_result(self, False, "Assertion failed", str(e))
        except Exception as e:
            return create_result(self, False, f"Exception: {type(e).__name__}", str(e))
