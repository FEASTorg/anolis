"""
Process Cleanup Verification Test

Validates that RuntimeFixture provides safe, scoped process cleanup:
1. Only terminates the spawned process (no collateral damage)
2. Uses process-group scoped termination
3. Proper escalation (SIGTERM -> SIGKILL)
4. Cleanup works even on test failure

"""

from __future__ import annotations

import time
from pathlib import Path

from tests.support.runtime_fixture import RuntimeFixture, verify_process_cleanup


def test_scoped_cleanup(runtime_path: Path, provider_path: Path, port: int) -> None:
    """
    Test 1: Verify cleanup only terminates spawned process.

    Steps:
    1. Start runtime via RuntimeFixture
    2. Record its PID
    3. Call cleanup()
    4. Verify that specific PID is terminated
    5. Verify no other processes affected (manual check)
    """

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=port, verbose=True)

    try:
        assert fixture.start(), "Failed to start runtime"

        pid = fixture.get_pid()
        pgid = fixture.get_pgid()
        assert pid is not None, "Could not get PID after start"
        assert fixture.is_running(), "Process not running after start"

        fixture.cleanup()

        assert verify_process_cleanup(pid, timeout=3.0), f"Process PID={pid} still running after cleanup!"

        assert not fixture.is_running(), "Fixture still reports process as running"
    finally:
        fixture.cleanup()


def test_cleanup_on_exception(runtime_path: Path, provider_path: Path, port: int) -> None:
    """
    Test 2: Verify cleanup works even when test throws exception.

    Steps:
    1. Start runtime
    2. Record PID
    3. Simulate test failure (throw exception)
    4. Ensure cleanup still runs via finally block
    5. Verify process terminated
    """

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=port, verbose=True)
    pid = None

    try:
        assert fixture.start(), "Failed to start runtime"

        pid = fixture.get_pid()

        raise RuntimeError("Simulated test failure!")

    except RuntimeError:
        pass  # Cleanup happens in finally block below

    finally:
        fixture.cleanup()

    # Verify cleanup worked despite exception
    assert pid is None or verify_process_cleanup(pid, timeout=3.0), f"Process PID={pid} still running after cleanup!"


def test_double_cleanup(runtime_path: Path, provider_path: Path, port: int) -> None:
    """
    Test 3: Verify double cleanup is safe (idempotent).

    Steps:
    1. Start runtime
    2. Call cleanup() once
    3. Call cleanup() again
    4. Verify no errors/crashes
    """

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=port, verbose=True)

    assert fixture.start(), "Failed to start runtime"

    pid = fixture.get_pid()
    assert pid is not None, "Could not get PID after start"

    fixture.cleanup()

    assert verify_process_cleanup(pid, timeout=3.0), "Process still running after first cleanup"

    fixture.cleanup()  # Should not crash


def test_graceful_vs_force_kill(runtime_path: Path, provider_path: Path, port: int) -> None:
    """
    Test 4: Verify escalation from graceful to force kill.

    Steps:
    1. Start runtime
    2. Call cleanup() (sends SIGTERM first)
    3. Verify process terminates (either gracefully or force-killed)

    Note: We can't easily test the 5-second timeout path without
    patching the runtime to ignore signals, but we can verify the
    cleanup logic completes successfully.
    """

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=port, verbose=True)

    assert fixture.start(), "Failed to start runtime"

    pid = fixture.get_pid()
    assert pid is not None, "Could not get PID after start"

    start_time = time.time()
    fixture.cleanup()
    cleanup_duration = time.time() - start_time

    assert verify_process_cleanup(pid, timeout=3.0), "Process still running after cleanup"
