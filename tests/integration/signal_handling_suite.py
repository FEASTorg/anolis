"""
Signal Handling Integration Test

Validates that the runtime gracefully handles SIGINT and SIGTERM signals
using async-signal-safe atomic flag pattern.

Tests:
1. SIGINT triggers clean shutdown (Ctrl+C simulation)
2. SIGTERM triggers clean shutdown (graceful termination)
3. No hangs or crashes during signal handling
4. Shutdown completes within reasonable timeout

"""

from __future__ import annotations

import signal
import sys
import time
from pathlib import Path

from tests.support.runtime_fixture import RuntimeFixture


def test_signal_handling(
    runtime_path: str,
    provider_path: str,
    test_signal: signal.Signals,
    signal_name: str,
    port: int,
) -> None:
    """Verify runtime responds to signal and shuts down cleanly."""
    fixture_config = Path(__file__).parent / "fixtures" / "provider-sim-default.yaml"
    config = {
        "runtime": {},
        "http": {"enabled": True, "bind": "127.0.0.1", "port": port},
        "providers": [
            {
                "id": "sim",
                "command": str(provider_path).replace("\\", "/"),
                "args": ["--config", str(fixture_config).replace("\\", "/")],
            }
        ],
        "polling": {"interval_ms": 1000},
        "logging": {"level": "info"},
        "automation": {"enabled": False},
        "telemetry": {"enabled": False},
    }
    fixture = RuntimeFixture(Path(runtime_path), Path(provider_path), http_port=port, config_dict=config)

    try:
        assert fixture.start(), "Failed to start runtime"

        capture = fixture.get_output_capture()
        assert capture is not None, "No output capture available"

        if not capture.wait_for_marker("Runtime Ready", timeout=15):
            output_tail = capture.get_recent_output(80)
            raise AssertionError(f"Runtime did not become ready within 15s\nOutput tail:\n{output_tail}")

        time.sleep(0.5)

        proc_info = fixture.process_info
        assert proc_info is not None and proc_info.process is not None, "No process info available"
        proc = proc_info.process

        if sys.platform == "win32":
            proc.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            proc.send_signal(test_signal)

        start_time = time.time()
        timeout = 5.0
        while time.time() - start_time < timeout:
            if not fixture.is_running():
                break
            time.sleep(0.1)

        assert not fixture.is_running(), f"Runtime hung after {signal_name} (timeout after 5s)"

        exit_code = proc.returncode if proc.returncode is not None else -1
        output = capture.get_all_output()

        has_shutdown_msg = "Signal received" in output or "stopping" in output.lower() or "Shutting down" in output
        if has_shutdown_msg or exit_code == 0:
            return
        assert exit_code in [0, 255], f"Unexpected exit code after {signal_name}: {exit_code}"
    finally:
        fixture.cleanup()
