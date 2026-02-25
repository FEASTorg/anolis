"""
Scenario Base Class

Provides common infrastructure for validation scenarios:
- HTTP API helpers
- Assertion utilities
- State verification
- Cleanup helpers
"""

import time
from dataclasses import dataclass
from typing import Any, Dict, List

import requests

from tests.support.api_helpers import (
    assert_mode as api_assert_mode,
    call_device_function,
    get_all_state as api_get_all_state,
    get_capabilities as api_get_capabilities,
    get_devices as api_get_devices,
    get_runtime_status as api_get_runtime_status,
    get_state as api_get_state,
    set_mode as api_set_mode,
)


@dataclass
class ScenarioResult:
    """Result of a scenario execution"""

    name: str
    passed: bool
    duration_seconds: float
    message: str = ""
    details: str = ""


class ScenarioBase:
    """
    Base class for validation scenarios.

    Each scenario should:
    1. Inherit from ScenarioBase
    2. Implement run() method
    3. Use assertion helpers for validation
    4. Clean up state in cleanup() or use try/finally
    """

    def __init__(self, base_url: str):
        """
        Initialize scenario with runtime base URL.

        Args:
            base_url: Base URL of anolis runtime (e.g., "http://localhost:8080")
        """
        self.base_url = base_url.rstrip("/")
        self.name = self.__class__.__name__
        self.start_time = 0.0

    def run(self) -> ScenarioResult:
        """
        Execute the scenario. Must be implemented by subclass.

        Returns:
            ScenarioResult with pass/fail status and diagnostics
        """
        raise NotImplementedError(f"{self.name} must implement run()")

    def setup(self):
        """
        Called before run(). Override to perform scenario-specific setup.
        Default implementation ensures runtime is in MANUAL mode with no faults.
        """
        # Ensure MANUAL mode
        self.set_mode("MANUAL")

        # Clear any injected faults
        self.call_function("sim0", "chaos_control", "clear_faults", {})

    def cleanup(self):
        """
        Called after run() (even if run fails). Override for scenario-specific cleanup.
        Default implementation clears faults and returns to MANUAL mode.
        """
        try:
            self.call_function("sim0", "chaos_control", "clear_faults", {})
            self.set_mode("MANUAL")
        except Exception:
            pass  # Best effort cleanup

    # -----------------------------
    # HTTP API Helpers
    # -----------------------------

    def get_devices(self) -> List[Dict[str, Any]]:
        """Get list of all devices from runtime."""
        devices = api_get_devices(self.base_url, timeout=5)
        if devices is None:
            raise RuntimeError("Failed to fetch devices from runtime")
        return devices

    def get_capabilities(self, provider: str, device: str) -> Dict[str, Any]:
        """Get device capabilities (signals and functions)."""
        return api_get_capabilities(self.base_url, provider, device, timeout=5)

    def get_state(self, provider: str, device: str) -> Dict[str, Any]:
        """Get normalized device state."""
        return api_get_state(self.base_url, provider, device, timeout=5)

    def get_all_state(self) -> Dict[str, Any]:
        """Get state of all devices."""
        return api_get_all_state(self.base_url, timeout=5)

    def call_function(self, provider: str, device: str, function: Any, args: Dict[str, Any]) -> Dict[str, Any]:
        """Call a device function by id or name."""
        return call_device_function(self.base_url, provider, device, function, args, timeout=20)

    def get_runtime_status(self) -> Dict[str, Any]:
        """Get runtime status."""
        status = api_get_runtime_status(self.base_url, timeout=5)
        if status is None:
            raise RuntimeError("Failed to fetch runtime status")
        return status

    def set_mode(self, mode: str):
        """Set runtime control mode (MANUAL or AUTO)."""
        if not api_set_mode(self.base_url, mode, timeout=5):
            raise RuntimeError(f"Failed to set runtime mode to {mode}")

    def wait_for_mode(self, expected_mode: str, timeout: float = 5.0) -> bool:
        """Wait for runtime to reach expected mode."""
        return api_assert_mode(self.base_url, expected_mode, timeout)

    # -----------------------------
    # Assertion Helpers
    # -----------------------------

    def assert_equal(self, actual: Any, expected: Any, message: str = ""):
        """Assert that actual equals expected."""
        assert actual == expected, message or f"Expected {expected}, got {actual}"

    def assert_true(self, condition: bool, message: str = ""):
        """Assert that condition is True."""
        assert condition, message or "Condition is False"

    def assert_false(self, condition: bool, message: str = ""):
        """Assert that condition is False."""
        assert not condition, message or "Condition is True"

    def assert_in(self, item: Any, collection: Any, message: str = ""):
        """Assert that item is in collection."""
        assert item in collection, message or f"{item} not in {collection}"

    def assert_device_exists(self, provider: str, device: str):
        """Assert that a device exists."""
        devices = self.get_devices()
        for d in devices:
            if d.get("provider_id") == provider and d.get("device_id") == device:
                return
        raise AssertionError(f"Device {provider}/{device} not found")

    def assert_signal_exists(self, provider: str, device: str, signal_id: str):
        """Assert that a signal exists on a device."""
        state = self.get_state(provider, device)
        signals = state.get("signals", [])
        for sig in signals:
            if sig.get("signal_id") == signal_id:
                return
        raise AssertionError(f"Signal {signal_id} not found on {provider}/{device}")

    def assert_signal_value(
        self,
        provider: str,
        device: str,
        signal_id: str,
        expected_value: Any,
        tolerance: float = 0.001,
    ):
        """
        Assert that a signal has an expected value.
        For floats, uses tolerance for comparison.
        """
        state = self.get_state(provider, device)
        signals = state.get("signals", [])

        for sig in signals:
            if sig.get("signal_id") == signal_id:
                actual = sig.get("value")

                # Handle float comparison with tolerance
                if isinstance(expected_value, float) and isinstance(actual, (int, float)):
                    assert abs(actual - expected_value) <= tolerance, (
                        f"Signal {signal_id}: expected {expected_value}, got {actual} (tolerance {tolerance})"
                    )
                    return

                # Exact comparison for other types
                assert actual == expected_value, f"Signal {signal_id}: expected {expected_value}, got {actual}"
                return

        raise AssertionError(f"Signal {signal_id} not found")

    def assert_mode(self, expected_mode: str):
        """Assert that runtime is in expected mode."""
        status = self.get_runtime_status()
        actual = status.get("mode")
        assert actual == expected_mode, f"Mode: expected {expected_mode}, got {actual}"

    def assert_http_error(self, status_code: int, func, *args, **kwargs):
        """
        Assert that a function call raises an HTTP error with expected status code.

        Args:
            status_code: Expected HTTP status code
            func: Function to call
            *args, **kwargs: Arguments to pass to func
        """
        try:
            func(*args, **kwargs)
            raise AssertionError(f"Expected HTTP {status_code} but call succeeded")
        except requests.HTTPError as e:
            assert e.response.status_code == status_code, f"Expected HTTP {status_code}, got {e.response.status_code}"
            # Expected error occurred
        except Exception as e:
            raise AssertionError(f"Expected HTTP {status_code} but call raised {type(e).__name__}") from e

    # -----------------------------
    # Utility Helpers
    # -----------------------------

    def sleep(self, seconds: float):
        """Sleep for specified seconds (for readability in scenarios)."""
        time.sleep(seconds)

    def poll_until(self, condition_func, timeout: float = 5.0, interval: float = 0.1) -> bool:
        """
        Poll until condition function returns True or timeout.

        Args:
            condition_func: Function that returns True when condition met
            timeout: Maximum time to wait
            interval: Polling interval

        Returns:
            True if condition met, False if timeout
        """
        start = time.time()
        while time.time() - start < timeout:
            try:
                if condition_func():
                    return True
            except Exception:
                # Swallow transient errors while polling
                pass
            time.sleep(interval)
        return False


def create_result(scenario: ScenarioBase, passed: bool, message: str = "", details: str = "") -> ScenarioResult:
    """
    Create a ScenarioResult for a scenario.

    Args:
        scenario: Scenario instance
        passed: Whether scenario passed
        message: Short summary message
        details: Detailed information about failure

    Returns:
        ScenarioResult instance
    """
    duration = time.time() - scenario.start_time
    return ScenarioResult(
        name=scenario.name,
        passed=passed,
        duration_seconds=duration,
        message=message,
        details=details,
    )
