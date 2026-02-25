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

import threading
import time
from typing import Any, Dict

from .base import ScenarioBase


class MultiDeviceConcurrency(ScenarioBase):
    """Multiple devices polled and controlled concurrently without deadlock."""

    def run(self) -> None:
        """Execute multi-device concurrency scenario."""
        # Step 1: Verify all devices are accessible
        devices = self.get_devices()
        device_ids = [d.get("device_id") for d in devices]

        expected_devices = ["tempctl0", "motorctl0", "relayio0", "analogsensor0"]
        for expected in expected_devices:
            assert expected in device_ids, f"Device {expected} not found"

        # Step 2: Concurrent state polling from all devices
        results_lock = threading.Lock()
        poll_results: Dict[str, Any] = {}
        poll_errors: list[tuple[str, str]] = []

        def poll_device(device_id: str) -> None:
            try:
                start = time.time()
                state = self.get_state("sim0", device_id)
                latency = time.time() - start
                with results_lock:
                    poll_results[device_id] = {
                        "success": True,
                        "latency": latency,
                        "signal_count": len(state.get("signals", [])),
                    }
            except Exception as e:
                with results_lock:
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
        assert len(poll_errors) == 0, f"Concurrent polls failed: {poll_errors}"
        assert len(poll_results) == len(expected_devices), (
            f"Not all devices polled: {len(poll_results)}/{len(expected_devices)}"
        )

        # Verify all devices returned signals
        for device_id, result in poll_results.items():
            assert result["signal_count"] > 0, f"Device {device_id} returned no signals"

        # Step 3: Concurrent function calls to multiple devices
        call_results: Dict[str, Dict[str, Any]] = {}
        call_errors: list[tuple[str, str, str]] = []

        def _invoke_concurrent(device_id: str, function_name: str, args: Dict[str, Any]) -> None:
            try:
                start = time.time()
                result = self.call_function("sim0", device_id, function_name, args)
                latency = time.time() - start
                with results_lock:
                    call_results[device_id] = {
                        "success": result.get("status") == "OK",
                        "latency": latency,
                        "status": result.get("status"),
                    }
            except Exception as e:
                with results_lock:
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
            t = threading.Thread(target=_invoke_concurrent, args=(device_id, function_name, args))
            threads.append(t)
            t.start()

        # Wait for all calls to complete
        for t in threads:
            t.join(timeout=5.0)

        # Verify all calls succeeded
        assert len(call_errors) == 0, f"Concurrent function calls failed: {call_errors}"

        for device_id, result in call_results.items():
            success = result.get("success")
            assert bool(success), f"Function call to {device_id} failed: {result.get('status')}"

        # Step 4: Verify all state changes were applied
        self.sleep(0.3)  # Allow state to propagate

        # Check tempctl0 relay1 state
        state = self.get_state("sim0", "tempctl0")
        relay1_state = None
        for sig in state["signals"]:
            if sig.get("signal_id") == "relay1_state":
                relay1_state = sig.get("value")
                break
        assert relay1_state is True, "tempctl0 relay1 not updated"

        # Check motorctl0 duty
        state = self.get_state("sim0", "motorctl0")
        motor1_duty = None
        for sig in state["signals"]:
            if sig.get("signal_id") == "motor1_duty":
                motor1_duty = sig.get("value")
                break
        assert motor1_duty is not None and abs(motor1_duty - 0.5) < 0.01, (
            f"motorctl0 duty not updated: expected 0.5, got {motor1_duty}"
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
        assert relay_ch1 is True, "relayio0 ch1 not updated"
        assert relay_ch2 is False, "relayio0 ch2 not updated"

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
                    self.call_function("sim0", "relayio0", "set_relay_ch1", {"enabled": i % 2 == 0})
                else:
                    self.get_state("sim0", "analogsensor0")
            except Exception as e:
                rapid_errors.append((i, str(e)))

        # Should have no errors under rapid sequential load
        assert len(rapid_errors) == 0, f"Rapid sequential operations failed: {rapid_errors}"

        # Aggregate latency metrics are intentionally not asserted here; this scenario validates correctness.
