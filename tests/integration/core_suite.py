"""Core runtime integration checks (pytest-oriented)."""

import time
from pathlib import Path
from typing import Callable, List, Tuple

from tests.support.api_helpers import (
    assert_device_count,
    assert_http_available,
    assert_provider_available,
    get_device_state,
    get_devices,
    get_runtime_status,
    wait_for_condition,
)
from tests.support.runtime_fixture import RuntimeFixture


class CoreFeatureTester:
    """Core runtime check harness."""

    def __init__(self, runtime_path: Path, provider_path: Path, timeout: float = 30.0, port: int = 8080):
        self.timeout = timeout
        self.port = port
        self.base_url = f"http://127.0.0.1:{port}"
        self.fixture = RuntimeFixture(runtime_path, provider_path, http_port=port)

    def start_runtime(self) -> bool:
        return self.fixture.start()

    def cleanup(self) -> None:
        self.fixture.cleanup()

    def _output_tail(self, lines: int = 80) -> str:
        capture = self.fixture.get_output_capture()
        if capture is None:
            return "(no output capture)"
        return capture.get_recent_output(lines)

    def check_http_server_available(self) -> None:
        assert assert_http_available(self.base_url, timeout=10), (
            "HTTP server did not become available\n" + self._output_tail()
        )

    def check_provider_available(self) -> None:
        assert assert_provider_available(self.base_url, "sim0", timeout=20), (
            "Provider sim0 not available via API\n" + self._output_tail()
        )
        status = get_runtime_status(self.base_url)
        assert status is not None, "Could not fetch runtime status"
        providers = status.get("providers", [])
        assert len(providers) > 0, "Runtime status reported no providers"

    def check_device_discovery(self) -> None:
        assert assert_device_count(self.base_url, expected_count=0, min_count=1, timeout=15), (
            "No devices discovered via API\n" + self._output_tail()
        )
        devices = get_devices(self.base_url)
        assert devices is not None and len(devices) > 0, "Expected discovered devices from /v0/devices"

    def check_state_cache_polling(self) -> None:
        devices = get_devices(self.base_url) or []
        assert devices, "No devices available to validate state cache polling"

        first = devices[0]
        provider_id = first.get("provider_id")
        device_id = first.get("device_id")
        assert provider_id and device_id, f"Device entry missing identifiers: {first}"

        polling_works = wait_for_condition(
            lambda: get_device_state(self.base_url, str(provider_id), str(device_id), timeout=2) is not None,
            timeout=5.0,
            interval=0.2,
            description="State cache populated",
        )
        assert polling_works, f"State cache was not populated for {provider_id}/{device_id}"

    def check_runtime_ready(self) -> None:
        status = get_runtime_status(self.base_url)
        devices = get_devices(self.base_url) or []
        assert status is not None, "Runtime status endpoint unavailable"
        assert len(devices) > 0, "Runtime has no devices after startup"

    def check_process_stability(self) -> None:
        deadline = time.time() + 5
        while time.time() < deadline:
            assert self.fixture.is_running(), "Runtime process exited unexpectedly during stability window"
            time.sleep(0.1)


CoreCheck = Tuple[str, Callable[[CoreFeatureTester], None]]

CORE_CHECKS: List[CoreCheck] = [
    ("http_server_available", CoreFeatureTester.check_http_server_available),
    ("provider_available", CoreFeatureTester.check_provider_available),
    ("device_discovery", CoreFeatureTester.check_device_discovery),
    ("state_cache_polling", CoreFeatureTester.check_state_cache_polling),
    ("runtime_ready", CoreFeatureTester.check_runtime_ready),
    ("process_stability", CoreFeatureTester.check_process_stability),
]
