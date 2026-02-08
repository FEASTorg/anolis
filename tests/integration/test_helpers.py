#!/usr/bin/env python3
"""
Anolis Integration Test Helpers

Provides reusable API-based test assertion functions for integration tests.
These helpers poll HTTP endpoints to verify runtime state, replacing brittle
log text matching with proper API-based oracles.

Philosophy:
- Primary assertions use HTTP API state (behavior verification)
- Logs are for debugging context only (not pass/fail criteria)
- Tests should be resilient to log format changes
"""

import time
from typing import Any, Callable, Dict, List, Optional

import requests


def wait_for_condition(
    condition_func: Callable[[], bool],
    timeout: float = 5.0,
    interval: float = 0.1,
    description: str = "condition",
) -> bool:
    """
    Poll a condition function until it returns True or timeout expires.

    Args:
        condition_func: Function that returns True when condition is met
        timeout: Maximum time to wait in seconds
        interval: Time between checks in seconds
        description: Human-readable description for error messages

    Returns:
        True if condition met before timeout, False otherwise
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            if condition_func():
                return True
        except Exception:
            # Condition check failed, keep retrying
            pass
        time.sleep(interval)
    return False


def assert_http_available(base_url: str, timeout: float = 10.0) -> bool:
    """
    Wait for HTTP server to become available.

    Args:
        base_url: Base URL of runtime HTTP server (e.g., "http://127.0.0.1:8080")
        timeout: Maximum time to wait in seconds

    Returns:
        True if HTTP server responds, False if timeout
    """

    def check_http():
        try:
            resp = requests.get(f"{base_url}/v0/runtime/status", timeout=1)
            return resp.status_code == 200
        except requests.exceptions.RequestException:
            return False

    return wait_for_condition(check_http, timeout=timeout, interval=0.2, description="HTTP server available")


def assert_provider_available(base_url: str, provider_id: str, timeout: float = 15.0) -> bool:
    """
    Wait for provider to be started and available in runtime status.

    Args:
        base_url: Base URL of runtime HTTP server
        provider_id: Provider ID to check (e.g., "sim0")
        timeout: Maximum time to wait in seconds

    Returns:
        True if provider available, False if timeout
    """

    def check_provider():
        try:
            resp = requests.get(f"{base_url}/v0/runtime/status", timeout=2)
            if resp.status_code != 200:
                return False
            status = resp.json()
            providers = status.get("providers", [])
            for p in providers:
                if p.get("provider_id") == provider_id and p.get("state") == "AVAILABLE":
                    return True
            return False
        except (requests.exceptions.RequestException, ValueError):
            return False

    return wait_for_condition(
        check_provider,
        timeout=timeout,
        interval=0.5,
        description=f"provider '{provider_id}' available",
    )


def assert_device_count(
    base_url: str, expected_count: int, timeout: float = 10.0, min_count: Optional[int] = None
) -> bool:
    """
    Wait for expected number of devices to be registered.

    Args:
        base_url: Base URL of runtime HTTP server
        expected_count: Exact device count expected
        timeout: Maximum time to wait in seconds
        min_count: If provided, check for at least this many devices instead of exact

    Returns:
        True if device count matches, False if timeout
    """

    def check_devices():
        try:
            resp = requests.get(f"{base_url}/v0/devices", timeout=2)
            if resp.status_code != 200:
                return False
            data = resp.json()
            devices = data.get("devices", [])
            actual_count = len(devices)
            if min_count is not None:
                return actual_count >= min_count
            return actual_count == expected_count
        except (requests.exceptions.RequestException, ValueError):
            return False

    desc = f"at least {min_count} devices" if min_count is not None else f"{expected_count} devices"
    return wait_for_condition(check_devices, timeout=timeout, interval=0.3, description=desc)


def assert_signal_value(
    base_url: str,
    device_id: str,
    signal_name: str,
    expected_value: Any,
    timeout: float = 5.0,
    tolerance: float = 0.01,
) -> bool:
    """
    Wait for signal to reach expected value.

    Args:
        base_url: Base URL of runtime HTTP server
        device_id: Device ID
        signal_name: Signal name within device
        expected_value: Expected signal value
        timeout: Maximum time to wait in seconds
        tolerance: For numeric values, acceptable difference from expected

    Returns:
        True if signal matches expected value, False if timeout
    """

    def check_signal():
        try:
            resp = requests.get(f"{base_url}/v0/state/{device_id}", timeout=2)
            if resp.status_code != 200:
                return False
            state = resp.json()
            for signal in state.get("signals", []):
                if signal.get("name") == signal_name:
                    actual = signal.get("value")
                    if isinstance(expected_value, (int, float)) and isinstance(actual, (int, float)):
                        return abs(actual - expected_value) <= tolerance
                    return actual == expected_value
            return False
        except (requests.exceptions.RequestException, ValueError):
            return False

    return wait_for_condition(
        check_signal,
        timeout=timeout,
        interval=0.2,
        description=f"signal {device_id}.{signal_name} == {expected_value}",
    )


def assert_mode(base_url: str, expected_mode: str, timeout: float = 5.0) -> bool:
    """
    Wait for runtime mode to reach expected value.

    Args:
        base_url: Base URL of runtime HTTP server
        expected_mode: Expected mode string (MANUAL, AUTO, IDLE, FAULT)
        timeout: Maximum time to wait in seconds

    Returns:
        True if mode matches, False if timeout
    """

    def check_mode():
        try:
            resp = requests.get(f"{base_url}/v0/mode", timeout=2)
            if resp.status_code != 200:
                return False
            mode_data = resp.json()
            return mode_data.get("mode") == expected_mode
        except (requests.exceptions.RequestException, ValueError):
            return False

    return wait_for_condition(check_mode, timeout=timeout, interval=0.1, description=f"mode == {expected_mode}")


def get_runtime_status(base_url: str, timeout: float = 2.0) -> Optional[Dict[str, Any]]:
    """
    Get current runtime status.

    Args:
        base_url: Base URL of runtime HTTP server
        timeout: Request timeout in seconds

    Returns:
        Status dict or None if request fails
    """
    try:
        resp = requests.get(f"{base_url}/v0/runtime/status", timeout=timeout)
        if resp.status_code == 200:
            return resp.json()
    except (requests.exceptions.RequestException, ValueError):
        pass
    return None


def get_devices(base_url: str, timeout: float = 2.0) -> Optional[List[Dict[str, Any]]]:
    """
    Get list of registered devices.

    Args:
        base_url: Base URL of runtime HTTP server
        timeout: Request timeout in seconds

    Returns:
        List of device dicts or None if request fails
    """
    try:
        resp = requests.get(f"{base_url}/v0/devices", timeout=timeout)
        if resp.status_code == 200:
            data = resp.json()
            # API returns {"status": {...}, "devices": [...]}
            return data.get("devices", [])
    except (requests.exceptions.RequestException, ValueError):
        pass
    return None


def get_device_state(base_url: str, device_id: str, timeout: float = 2.0) -> Optional[Dict[str, Any]]:
    """
    Get state for a specific device.

    Args:
        base_url: Base URL of runtime HTTP server
        device_id: Device ID
        timeout: Request timeout in seconds

    Returns:
        Device state dict or None if request fails
    """
    try:
        resp = requests.get(f"{base_url}/v0/state/{device_id}", timeout=timeout)
        if resp.status_code == 200:
            return resp.json()
    except (requests.exceptions.RequestException, ValueError):
        pass
    return None


def get_mode(base_url: str, timeout: float = 2.0) -> Optional[str]:
    """
    Get current runtime mode.

    Args:
        base_url: Base URL of runtime HTTP server
        timeout: Request timeout in seconds

    Returns:
        Mode string (MANUAL/AUTO/IDLE/FAULT) or None if request fails
    """
    try:
        resp = requests.get(f"{base_url}/v0/mode", timeout=timeout)
        if resp.status_code == 200:
            mode_data = resp.json()
            return mode_data.get("mode")
    except (requests.exceptions.RequestException, ValueError):
        pass
    return None


def set_mode(base_url: str, mode: str, timeout: float = 2.0) -> bool:
    """
    Set runtime mode.

    Args:
        base_url: Base URL of runtime HTTP server
        mode: Target mode (MANUAL/AUTO/IDLE/FAULT)
        timeout: Request timeout in seconds

    Returns:
        True if mode set successfully, False otherwise
    """
    try:
        resp = requests.post(
            f"{base_url}/v0/mode",
            json={"mode": mode},
            headers={"Content-Type": "application/json"},
            timeout=timeout,
        )
        return resp.status_code == 200
    except requests.exceptions.RequestException:
        return False
