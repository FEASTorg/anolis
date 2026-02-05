"""
Scenario Base Class

Provides common infrastructure for validation scenarios:
- HTTP API helpers
- Assertion utilities
- State verification
- Cleanup helpers
"""

import time
from typing import Any, Dict, List, Optional
from dataclasses import dataclass


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
        self.base_url = base_url.rstrip('/')
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
        self.call_function("sim0", "sim_control", "clear_faults", {})
        
    def cleanup(self):
        """
        Called after run() (even if run fails). Override for scenario-specific cleanup.
        Default implementation clears faults and returns to MANUAL mode.
        """
        try:
            self.call_function("sim0", "sim_control", "clear_faults", {})
            self.set_mode("MANUAL")
        except Exception:
            pass  # Best effort cleanup
            
    # -----------------------------
    # HTTP API Helpers
    # -----------------------------
    
    def get_devices(self) -> List[Dict[str, Any]]:
        """Get list of all devices from runtime."""
        import requests
        url = f"{self.base_url}/v0/devices"
        resp = requests.get(url, timeout=5)
        resp.raise_for_status()
        data = resp.json()
        return data.get("devices", [])
        
    def get_capabilities(self, provider: str, device: str) -> Dict[str, Any]:
        """Get device capabilities."""
        import requests
        url = f"{self.base_url}/v0/devices/{provider}/{device}/capabilities"
        resp = requests.get(url, timeout=5)
        resp.raise_for_status()
        return resp.json()
        
    def get_state(self, provider: str, device: str) -> Dict[str, Any]:
        """Get device state (all signals)."""
        import requests
        url = f"{self.base_url}/v0/state/{provider}/{device}"
        resp = requests.get(url, timeout=5)
        resp.raise_for_status()
        return resp.json()
        
    def get_all_state(self) -> Dict[str, Any]:
        """Get state of all devices."""
        import requests
        url = f"{self.base_url}/v0/state"
        resp = requests.get(url, timeout=5)
        resp.raise_for_status()
        return resp.json()
        
    def call_function(self, provider: str, device: str, function: str, 
                     args: Dict[str, Any]) -> Dict[str, Any]:
        """Call a device function."""
        import requests
        url = f"{self.base_url}/v0/call"
        payload = {
            "provider": provider,
            "device": device,
            "function": function,
            "args": args
        }
        resp = requests.post(url, json=payload, timeout=10)
        resp.raise_for_status()
        return resp.json()
        
    def get_runtime_status(self) -> Dict[str, Any]:
        """Get runtime status."""
        import requests
        url = f"{self.base_url}/v0/runtime/status"
        resp = requests.get(url, timeout=5)
        resp.raise_for_status()
        return resp.json()
        
    def set_mode(self, mode: str):
        """Set runtime control mode (MANUAL or AUTO)."""
        import requests
        url = f"{self.base_url}/v0/runtime/mode"
        payload = {"mode": mode}
        resp = requests.post(url, json=payload, timeout=5)
        resp.raise_for_status()
        
    def wait_for_mode(self, expected_mode: str, timeout: float = 5.0) -> bool:
        """
        Wait for runtime to reach expected mode.
        
        Returns:
            True if mode reached, False if timeout
        """
        start = time.time()
        while time.time() - start < timeout:
            try:
                status = self.get_runtime_status()
                if status.get("mode") == expected_mode:
                    return True
            except Exception:
                pass
            time.sleep(0.1)
        return False
        
    # -----------------------------
    # Assertion Helpers
    # -----------------------------
    
    def assert_equal(self, actual: Any, expected: Any, message: str = ""):
        """Assert that actual equals expected."""
        if actual != expected:
            msg = message or f"Expected {expected}, got {actual}"
            raise AssertionError(msg)
            
    def assert_true(self, condition: bool, message: str = ""):
        """Assert that condition is True."""
        if not condition:
            raise AssertionError(message or "Condition is False")
            
    def assert_false(self, condition: bool, message: str = ""):
        """Assert that condition is False."""
        if condition:
            raise AssertionError(message or "Condition is True")
            
    def assert_in(self, item: Any, collection: Any, message: str = ""):
        """Assert that item is in collection."""
        if item not in collection:
            msg = message or f"{item} not in {collection}"
            raise AssertionError(msg)
            
    def assert_device_exists(self, provider: str, device: str):
        """Assert that a device exists."""
        devices = self.get_devices()
        for d in devices:
            if d.get("provider") == provider and d.get("device_id") == device:
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
        
    def assert_signal_value(self, provider: str, device: str, signal_id: str, 
                           expected_value: Any, tolerance: float = 0.001):
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
                    if abs(actual - expected_value) <= tolerance:
                        return
                    raise AssertionError(
                        f"Signal {signal_id}: expected {expected_value}, "
                        f"got {actual} (tolerance {tolerance})"
                    )
                    
                # Exact comparison for other types
                if actual == expected_value:
                    return
                    
                raise AssertionError(
                    f"Signal {signal_id}: expected {expected_value}, got {actual}"
                )
                
        raise AssertionError(f"Signal {signal_id} not found")
        
    def assert_mode(self, expected_mode: str):
        """Assert that runtime is in expected mode."""
        status = self.get_runtime_status()
        actual = status.get("mode")
        if actual != expected_mode:
            raise AssertionError(f"Mode: expected {expected_mode}, got {actual}")
            
    def assert_http_error(self, status_code: int, func, *args, **kwargs):
        """
        Assert that a function call raises an HTTP error with expected status code.
        
        Args:
            status_code: Expected HTTP status code
            func: Function to call
            *args, **kwargs: Arguments to pass to func
        """
        import requests
        try:
            func(*args, **kwargs)
            raise AssertionError(f"Expected HTTP {status_code} but call succeeded")
        except requests.HTTPError as e:
            if e.response.status_code != status_code:
                raise AssertionError(
                    f"Expected HTTP {status_code}, got {e.response.status_code}"
                )
            # Expected error occurred
            
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
                pass
            time.sleep(interval)
        return False


def create_result(scenario: ScenarioBase, passed: bool, message: str = "", 
                 details: str = "") -> ScenarioResult:
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
        details=details
    )
