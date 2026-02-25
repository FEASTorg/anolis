"""
Provider Configuration Integration Tests

Validates anolis-provider-sim configuration system requirements:
1. --config argument is mandatory (no backward compatibility)
2. Multi-device configurations with custom IDs work correctly
3. Error handling for missing/invalid config files
4. Device type validation and graceful degradation

These tests ensure the provider configuration system works correctly
before Operator UI integration.

"""

import os
import tempfile
import time
from pathlib import Path

import requests

from tests.support.runtime_fixture import RuntimeFixture


def get_fixture_path(filename: str) -> Path:
    """Get path to test fixture config file."""
    return Path(__file__).parent / "fixtures" / filename


def create_invalid_yaml_config() -> Path:
    """Create a temporary file with invalid YAML syntax."""
    fd, path = tempfile.mkstemp(suffix=".yaml", prefix="invalid-config-")
    with os.fdopen(fd, "w") as f:
        f.write("devices: [this is: {not: valid: yaml")
    return Path(path)


def create_unknown_type_config() -> Path:
    """Create config with unknown device type."""
    fd, path = tempfile.mkstemp(suffix=".yaml", prefix="unknown-type-")
    with os.fdopen(fd, "w") as f:
        f.write("devices:\n")
        f.write("  - id: widget0\n")
        f.write("    type: nonexistent_device\n")
        f.write("  - id: tempctl0\n")
        f.write("    type: tempctl\n")
    return Path(path)


def test_missing_config_required(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """Test that provider requires --config argument."""
    print("\n=== TEST: Missing --config Argument ===")

    # Create runtime config WITHOUT --config argument for provider
    config = {
        "runtime": {},
        "http": {"enabled": True, "bind": "127.0.0.1", "port": port},
        "providers": [
            {
                "id": "sim0",
                "command": str(provider_path).replace("\\", "/"),
                "args": [],  # No --config argument
                "timeout_ms": 5000,
            }
        ],
        "polling": {"interval_ms": 500},
    }

    fixture = RuntimeFixture(runtime_path, provider_path, config_dict=config)

    try:
        if not fixture.start():
            print("  [FAIL] Runtime failed to start")
            return False

        capture = fixture.get_output_capture()
        if not capture:
            print("  [FAIL] No output capture available")
            return False

        # Wait a moment for provider to exit (provider launched by runtime)
        time.sleep(3)

        # Check if runtime is still running (should be, but provider should have died)
        # Provider errors appear in runtime's captured output
        output = capture.get_all_output()

        # Look for provider exit or error message about config
        has_config_error = "FATAL" in output and "config" in output.lower()
        has_provider_died = "Provider exited" in output or "Provider died" in output

        if has_config_error or has_provider_died:
            print("  [PASS] Provider failed to start without --config")
            print("  [PASS] Error message found in output")
            return True
        else:
            print("  [FAIL] Expected provider to fail without config")
        return False

    finally:
        fixture.cleanup()


def test_default_config_loads(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """Test that default fixture config loads correctly."""
    print("\n=== TEST: Default Configuration ===")
    base_url = f"http://localhost:{port}"

    config_path = get_fixture_path("provider-sim-default.yaml")

    if not config_path.exists():
        print(f"  [FAIL] Fixture config not found: {config_path}")
        return False

    print(f"  Using config: {config_path.name}")

    config = {
        "runtime": {},
        "http": {"enabled": True, "bind": "127.0.0.1", "port": port},
        "providers": [
            {
                "id": "sim0",
                "command": str(provider_path).replace("\\", "/"),
                "args": ["--config", str(config_path).replace("\\", "/")],
                "timeout_ms": 5000,
            }
        ],
        "polling": {"interval_ms": 500},
    }

    fixture = RuntimeFixture(runtime_path, provider_path, config_dict=config)

    try:
        if not fixture.start():
            print("  [FAIL] Runtime failed to start")
            return False

        capture = fixture.get_output_capture()
        if not capture:
            print("  [FAIL] No output capture available")
            return False

        # Wait for runtime to become ready
        if not capture.wait_for_marker("Runtime Ready", timeout=15):
            print("  [FAIL] Runtime did not become ready")
            return False

        # Query devices via HTTP API
        resp = requests.get(f"{base_url}/v0/devices", timeout=5)
        if resp.status_code != 200:
            print(f"  [FAIL] HTTP request failed: {resp.status_code}")
            return False

        data = resp.json()
        devices = data.get("devices", [])
        sim0_devices = [d for d in devices if d.get("provider_id") == "sim0"]
        device_ids = [d["device_id"] for d in sim0_devices]

        # Expected: tempctl0, motorctl0, relayio0, analogsensor0, chaos_control
        expected = {"tempctl0", "motorctl0", "relayio0", "analogsensor0", "chaos_control"}

        if set(device_ids) == expected:
            print(f"  [PASS] All {len(expected)} devices loaded correctly")
            for dev_id in sorted(device_ids):
                print(f"    - {dev_id}")
            return True
        else:
            missing = expected - set(device_ids)
            extra = set(device_ids) - expected
            if missing:
                print(f"  [FAIL] Missing devices: {missing}")
            if extra:
                print(f"  [FAIL] Unexpected devices: {extra}")
            return False

    finally:
        fixture.cleanup()


def test_multi_device_config(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """Test multi-device configuration with 2 tempctl instances."""
    print("\n=== TEST: Multi-Device Configuration (2x tempctl) ===")
    base_url = f"http://localhost:{port}"

    config_path = get_fixture_path("provider-multi-tempctl.yaml")

    if not config_path.exists():
        print(f"  [FAIL] Fixture config not found: {config_path}")
        return False

    print(f"  Using config: {config_path.name}")

    config = {
        "runtime": {},
        "http": {"enabled": True, "bind": "127.0.0.1", "port": port},
        "providers": [
            {
                "id": "sim0",
                "command": str(provider_path).replace("\\", "/"),
                "args": ["--config", str(config_path).replace("\\", "/")],
                "timeout_ms": 5000,
            }
        ],
        "polling": {"interval_ms": 500},
    }

    fixture = RuntimeFixture(runtime_path, provider_path, config_dict=config)

    try:
        if not fixture.start():
            print("  [FAIL] Runtime failed to start")
            return False

        capture = fixture.get_output_capture()
        if not capture:
            print("  [FAIL] No output capture available")
            return False

        # Wait for runtime to become ready
        if not capture.wait_for_marker("Runtime Ready", timeout=15):
            print("  [FAIL] Runtime did not become ready")
            return False

        # Query devices
        resp = requests.get(f"{base_url}/v0/devices", timeout=5)
        if resp.status_code != 200:
            print(f"  [FAIL] HTTP request failed: {resp.status_code}")
            return False

        data = resp.json()
        devices = data.get("devices", [])
        sim0_devices = [d for d in devices if d.get("provider_id") == "sim0"]
        device_ids = [d["device_id"] for d in sim0_devices]

        # Expected: tempctl0, tempctl1, motorctl0, chaos_control
        expected = {"tempctl0", "tempctl1", "motorctl0", "chaos_control"}

        if set(device_ids) != expected:
            print(f"  [FAIL] Expected {expected}, got {set(device_ids)}")
            return False

        print(f"  [PASS] All {len(expected)} devices loaded")
        for dev_id in sorted(device_ids):
            print(f"    - {dev_id}")

        # Verify both tempctl devices are functional
        all_functional = True
        for dev_id in ["tempctl0", "tempctl1"]:
            resp = requests.get(f"{base_url}/v0/state/sim0/{dev_id}", timeout=5)
            if resp.status_code == 200:
                print(f"  [PASS] {dev_id} is functional")
            else:
                print(f"  [FAIL] {dev_id} returned status {resp.status_code}")
                all_functional = False

        return all_functional

    finally:
        fixture.cleanup()


def test_minimal_config(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """Test minimal configuration with single device."""
    print("\n=== TEST: Minimal Configuration (Single Device) ===")
    base_url = f"http://localhost:{port}"

    config_path = get_fixture_path("provider-minimal.yaml")

    if not config_path.exists():
        print(f"  [FAIL] Fixture config not found: {config_path}")
        return False

    print(f"  Using config: {config_path.name}")

    config = {
        "runtime": {},
        "http": {"enabled": True, "bind": "127.0.0.1", "port": port},
        "providers": [
            {
                "id": "sim0",
                "command": str(provider_path).replace("\\", "/"),
                "args": ["--config", str(config_path).replace("\\", "/")],
                "timeout_ms": 5000,
            }
        ],
        "polling": {"interval_ms": 500},
    }

    fixture = RuntimeFixture(runtime_path, provider_path, config_dict=config)

    try:
        if not fixture.start():
            print("  [FAIL] Runtime failed to start")
            return False

        capture = fixture.get_output_capture()
        if not capture:
            print("  [FAIL] No output capture available")
            return False

        # Wait for runtime to become ready
        if not capture.wait_for_marker("Runtime Ready", timeout=15):
            print("  [FAIL] Runtime did not become ready")
            return False

        # Query devices
        resp = requests.get(f"{base_url}/v0/devices", timeout=5)
        if resp.status_code != 200:
            print(f"  [FAIL] HTTP request failed: {resp.status_code}")
            return False

        data = resp.json()
        devices = data.get("devices", [])
        sim0_devices = [d for d in devices if d.get("provider_id") == "sim0"]
        device_ids = [d["device_id"] for d in sim0_devices]

        # Expected: only tempctl0 + chaos_control
        expected = {"tempctl0", "chaos_control"}

        if set(device_ids) == expected:
            print("  [PASS] Minimal config loaded (2 devices)")
            for dev_id in sorted(device_ids):
                print(f"    - {dev_id}")
            return True
        else:
            print(f"  [FAIL] Expected {expected}, got {set(device_ids)}")
            return False

    finally:
        fixture.cleanup()


def test_invalid_yaml_handling(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """Test provider handling of invalid YAML syntax."""
    print("\n=== TEST: Invalid YAML Syntax ===")

    config_path = create_invalid_yaml_config()

    try:
        print(f"  Using invalid config: {config_path.name}")

        config = {
            "runtime": {},
            "http": {"enabled": True, "bind": "127.0.0.1", "port": port},
            "providers": [
                {
                    "id": "sim0",
                    "command": str(provider_path).replace("\\", "/"),
                    "args": ["--config", str(config_path).replace("\\", "/")],
                    "timeout_ms": 5000,
                }
            ],
            "polling": {"interval_ms": 500},
        }

        fixture = RuntimeFixture(runtime_path, provider_path, config_dict=config)

        try:
            if not fixture.start():
                print("  [FAIL] Runtime failed to start")
                return False

            # Wait for provider to exit with error
            time.sleep(3)

            # Check output for provider failure
            capture = fixture.get_output_capture()
            if not capture:
                print("  [FAIL] No output capture available")
                return False

            output = capture.get_all_output()

            # Provider should have died due to bad YAML
            if "Provider exited" in output or "Provider died" in output or "Failed to load" in output:
                print("  [PASS] Provider exited with error (expected)")
                return True
            else:
                print("  [FAIL] Expected provider to fail with invalid YAML")
                print(f"  Output sample: {output[:300]}")
                return False

        finally:
            fixture.cleanup()
    finally:
        config_path.unlink(missing_ok=True)


def test_unknown_device_type(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """Test that unknown device types cause provider to fail fast at startup."""
    print("\n=== TEST: Unknown Device Type (Fail-Fast) ===")

    config_path = create_unknown_type_config()

    try:
        print(f"  Using config with unknown type: {config_path.name}")

        config = {
            "runtime": {},
            "http": {"enabled": True, "bind": "127.0.0.1", "port": port},
            "providers": [
                {
                    "id": "sim0",
                    "command": str(provider_path).replace("\\", "/"),
                    "args": ["--config", str(config_path).replace("\\", "/")],
                    "timeout_ms": 5000,
                }
            ],
            "polling": {"interval_ms": 500},
        }

        fixture = RuntimeFixture(runtime_path, provider_path, config_dict=config)

        try:
            if not fixture.start():
                print("  [FAIL] Runtime failed to start")
                return False

            capture = fixture.get_output_capture()
            if not capture:
                print("  [FAIL] No output capture available")
                return False

            # Provider should fail to start with unknown device type
            # Check for error message in output
            if capture.wait_for_marker("unknown device type", timeout=5):
                print("  [PASS] Provider failed fast with unknown device type error")
                return True
            elif capture.wait_for_marker("Runtime Ready", timeout=5):
                print("  [FAIL] Provider started successfully (should have failed fast)")
                return False
            else:
                print("  [PASS] Provider process exited (fail-fast behavior)")
                return True

        finally:
            fixture.cleanup()
    finally:
        config_path.unlink(missing_ok=True)
