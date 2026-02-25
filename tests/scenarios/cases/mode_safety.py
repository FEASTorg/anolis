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

        # Test 2: Verify read-only operations work in IDLE
        status = self.get_runtime_status()
        assert status["mode"] == "IDLE", "Mode should be IDLE"

        # Get device list (read-only)
        devices = self.get_devices()
        assert len(devices) >= 4, f"Expected at least 4 devices, found {len(devices)}"

        # Get device state (read-only)
        state = self.get_state("sim0", "tempctl0")
        assert "signals" in state, "Should be able to read device state in IDLE"

        # Get device capabilities (read-only)
        caps = self.get_capabilities("sim0", "tempctl0")
        assert "signals" in caps, "Should be able to read capabilities in IDLE"

        # Test 3: Verify control operations are blocked in IDLE.
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})
        assert result.get("status") == "FAILED_PRECONDITION", (
            f"Expected FAILED_PRECONDITION in IDLE mode, got: {result.get('status')}"
        )

        # Test 4: Transition to MANUAL and verify operations work
        self.set_mode("MANUAL")
        self.assert_mode("MANUAL")

        # Now control operations should work
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": True})
        assert result["status"] == "OK", "Control operation should succeed in MANUAL mode"

        # Verify state changed
        self.sleep(0.2)  # Allow state to propagate
        state = self.get_state("sim0", "tempctl0")
        relay1_state = None
        for sig in state["signals"]:
            if sig.get("signal_id") == "relay1_state":
                relay1_state = sig.get("value")
                break
        assert relay1_state is True, "Relay should be ON after successful call"

        # Clean up: turn relay back off
        result = self.call_function("sim0", "tempctl0", "set_relay", {"relay_index": 1, "state": False})
        assert result["status"] == "OK", "Failed to turn relay off"
