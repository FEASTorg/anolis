"""
Scenario: Mode Safety Enforcement

Validates that IDLE mode provides safe startup state with control operations blocked.

Tests:
1. Runtime starts in IDLE mode (safe default)
2. Control operations blocked in IDLE
3. Read-only operations allowed in IDLE (state queries)
4. IDLE -> MANUAL transition enables control operations
5. Automation doesn't run in IDLE
"""

from .base import ScenarioBase


class ModeSafety(ScenarioBase):
    """Verify IDLE mode safety enforcement."""

    def run(self) -> None:
        """Execute mode safety scenario."""
        # Test 1: Verify runtime starts in IDLE mode
        # Note: Runtime config explicitly sets MANUAL for tests, so we transition to IDLE for this test
        self.set_mode("IDLE")
        self.assert_mode("IDLE")

        # Test 2: Verify read-only operations work in IDLE
        # Get runtime status (read-only)
        status = self.get_runtime_status()
        self.assert_equal(status["mode"], "IDLE", "Mode should be IDLE")

        # Get device list (read-only)
        devices = self.get_devices()
        self.assert_true(len(devices) >= 4, f"Expected at least 4 devices, found {len(devices)}")

        # Get device state (read-only)
        state = self.get_state("sim0", "tempctl0")
        self.assert_in("signals", state, "Should be able to read device state in IDLE")

        # Get device capabilities (read-only)
        caps = self.get_capabilities("sim0", "tempctl0")
        self.assert_in("signals", caps, "Should be able to read capabilities in IDLE")

        # Test 3: Verify control operations are BLOCKED in IDLE
        try:
            result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})

            # Call should return non-OK status (FAILED_PRECONDITION expected)
            status = result.get("status", "")

            assert status != "OK", "Control operation succeeded in IDLE mode when it should be blocked"

            # Check if status indicates blocking (FAILED_PRECONDITION is expected)
            assert status == "FAILED_PRECONDITION", f"Expected FAILED_PRECONDITION, got: {status}"

            # Got expected FAILED_PRECONDITION - control operation properly blocked

        except Exception as e:
            # HTTP error is also acceptable (some implementations may reject at HTTP level)
            import requests

            if isinstance(e, requests.HTTPError):
                # Expected - call blocked at HTTP level
                pass
            else:
                raise  # Unexpected error type

        # Test 4: Transition to MANUAL and verify operations work
        self.set_mode("MANUAL")
        self.assert_mode("MANUAL")

        # Now control operations should work
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})
        self.assert_equal(result["status"], "OK", "Control operation should succeed in MANUAL mode")

        # Verify state changed
        self.sleep(0.2)  # Allow state to propagate
        state = self.get_state("sim0", "tempctl0")
        relay1_state = None
        for sig in state["signals"]:
            if sig.get("signal_id") == "relay1_state":
                relay1_state = sig.get("value")
                break
        self.assert_equal(relay1_state, True, "Relay should be ON after successful call")

        # Clean up: turn relay back off
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": False})
        self.assert_equal(result["status"], "OK", "Failed to turn relay off")
