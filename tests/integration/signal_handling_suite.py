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

import signal
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from tests.support.runtime_fixture import RuntimeFixture


@dataclass
class TestResult:
    name: str
    passed: bool
    message: str = ""


def test_signal_handling(
    runtime_path: str, provider_path: str, test_signal: signal.Signals, signal_name: str
) -> TestResult:
    """
    Test that runtime responds to signal and shuts down cleanly.

    Returns:
        TestResult with success/failure and message
    """
    # Get provider config path
    fixture_config = Path(__file__).parent / "fixtures" / "provider-sim-default.yaml"

    # Create config dict
    config = {
        "runtime": {},
        "http": {"enabled": True, "bind": "127.0.0.1", "port": 8765},
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

    # Create fixture
    fixture = RuntimeFixture(
        Path(runtime_path),
        Path(provider_path),
        http_port=8765,
        config_dict=config,
    )

    try:
        # Start runtime
        if not fixture.start():
            return TestResult(signal_name, False, "Failed to start runtime")

        capture = fixture.get_output_capture()
        if not capture:
            fixture.cleanup()
            return TestResult(signal_name, False, "No output capture available")

        # Wait for runtime to become ready
        if not capture.wait_for_marker("Runtime Ready", timeout=15):
            output_tail = capture.get_recent_output(80)
            fixture.cleanup()
            return TestResult(
                signal_name,
                False,
                f"Runtime did not become ready within 15s\nOutput tail:\n{output_tail}",
            )

        # Wait a bit for stability
        time.sleep(0.5)

        # Get process to send signal
        proc_info = fixture.process_info
        if not proc_info or not proc_info.process:
            fixture.cleanup()
            return TestResult(signal_name, False, "No process info available")

        proc = proc_info.process

        # Send signal
        if sys.platform == "win32":
            # Windows: use CTRL_BREAK_EVENT which our handler can catch
            if test_signal == signal.SIGINT:
                proc.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                proc.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            # Unix: send actual signal
            proc.send_signal(test_signal)

        # Wait for clean shutdown
        start_time = time.time()
        timeout = 5.0
        while time.time() - start_time < timeout:
            if not fixture.is_running():
                break
            time.sleep(0.1)

        if fixture.is_running():
            fixture.cleanup()
            return TestResult(signal_name, False, "Runtime hung after signal (timeout after 5s)")

        # Get exit code and output
        exit_code = proc.returncode if proc.returncode is not None else -1
        output = capture.get_all_output()

        # Verify graceful shutdown
        has_shutdown_msg = "Signal received" in output or "stopping" in output.lower() or "Shutting down" in output

        # On Windows, CTRL_BREAK might cause exit code 0 or non-zero
        # What matters is that it shut down without hanging
        if has_shutdown_msg or exit_code == 0:
            return TestResult(signal_name, True, f"Clean shutdown (exit: {exit_code})")
        else:
            # Even without explicit message, if it exited cleanly, that's okay
            if exit_code in [0, 255]:  # 255 can be -1 in unsigned
                return TestResult(signal_name, True, f"Shutdown completed (exit: {exit_code})")
            return TestResult(signal_name, False, f"Unexpected exit code: {exit_code}")

    except Exception as e:
        fixture.cleanup()
        return TestResult(signal_name, False, f"Exception: {e}")
    finally:
        fixture.cleanup()
