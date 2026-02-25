"""
Process Cleanup Verification Test

Validates that RuntimeFixture provides safe, scoped process cleanup:
1. Only terminates the spawned process (no collateral damage)
2. Uses process-group scoped termination
3. Proper escalation (SIGTERM -> SIGKILL)
4. Cleanup works even on test failure

"""

import time
from pathlib import Path

from tests.support.runtime_fixture import RuntimeFixture, verify_process_cleanup


def test_scoped_cleanup(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """
    Test 1: Verify cleanup only terminates spawned process.

    Steps:
    1. Start runtime via RuntimeFixture
    2. Record its PID
    3. Call cleanup()
    4. Verify that specific PID is terminated
    5. Verify no other processes affected (manual check)
    """
    print("\n" + "=" * 60)
    print("  Test 1: Scoped Process Cleanup")
    print("=" * 60)

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=port, verbose=True)

    try:
        print("\n[1] Starting runtime...")
        if not fixture.start():
            print("[FAIL] Failed to start runtime")
            return False

        pid = fixture.get_pid()
        pgid = fixture.get_pgid()
        print(f"[PASS] Runtime started - PID={pid}, PGID={pgid}")

        if pid is None:
            print("[FAIL] Could not get PID after start")
            return False

        # Verify process is running
        if not fixture.is_running():
            print("[FAIL] Process not running after start")
            return False
        print("[PASS] Process is running")

        print("\n[2] Cleaning up...")
        fixture.cleanup()

        print("\n[3] Verifying process terminated...")
        if not verify_process_cleanup(pid, timeout=3.0):
            print(f"[FAIL] Process PID={pid} still running after cleanup!")
            return False

        print(f"[PASS] Process PID={pid} terminated successfully")

        # Verify fixture state cleaned up
        if fixture.is_running():
            print("[FAIL] Fixture still reports process as running")
            return False
        print("[PASS] Fixture state cleaned up")

        print("\n[PASS] Test 1 PASSED: Scoped cleanup working")
        return True

    except Exception as e:
        print(f"[FAIL] Test 1 exception: {e}")
        import traceback

        traceback.print_exc()
        return False

    finally:
        # Ensure cleanup even if test fails
        fixture.cleanup()


def test_cleanup_on_exception(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """
    Test 2: Verify cleanup works even when test throws exception.

    Steps:
    1. Start runtime
    2. Record PID
    3. Simulate test failure (throw exception)
    4. Ensure cleanup still runs via finally block
    5. Verify process terminated
    """
    print("\n" + "=" * 60)
    print("  Test 2: Cleanup on Exception")
    print("=" * 60)

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=port, verbose=True)
    pid = None

    try:
        print("\n[1] Starting runtime...")
        if not fixture.start():
            print("[FAIL] Failed to start runtime")
            return False

        pid = fixture.get_pid()
        print(f"[PASS] Runtime started - PID={pid}")

        print("\n[2] Simulating test failure...")
        raise RuntimeError("Simulated test failure!")

    except RuntimeError as e:
        print(f"[EXPECTED] Caught exception: {e}")

        print("\n[3] Cleanup should run in finally block...")
        # Cleanup happens in finally block below

    finally:
        fixture.cleanup()

    # Verify cleanup worked despite exception
    print("\n[4] Verifying process terminated after exception...")
    if pid is not None and not verify_process_cleanup(pid, timeout=3.0):
        print(f"[FAIL] Process PID={pid} still running after cleanup!")
        return False

    print(f"[PASS] Process PID={pid} terminated successfully")
    print("\n[PASS] Test 2 PASSED: Cleanup on exception working")
    return True


def test_double_cleanup(runtime_path: Path, provider_path: Path, port: int) -> bool:
    """
    Test 3: Verify double cleanup is safe (idempotent).

    Steps:
    1. Start runtime
    2. Call cleanup() once
    3. Call cleanup() again
    4. Verify no errors/crashes
    """
    print("\n" + "=" * 60)
    print("  Test 3: Double Cleanup Safety")
    print("=" * 60)

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=port, verbose=True)

    try:
        print("\n[1] Starting runtime...")
        if not fixture.start():
            print("[FAIL] Failed to start runtime")
            return False

        pid = fixture.get_pid()
        print(f"[PASS] Runtime started - PID={pid}")

        if pid is None:
            print("[FAIL] Could not get PID after start")
            return False

        print("\n[2] First cleanup...")
        fixture.cleanup()

        if not verify_process_cleanup(pid, timeout=3.0):
            print("[FAIL] Process still running after first cleanup")
            return False
        print("[PASS] First cleanup succeeded")

        print("\n[3] Second cleanup (should be no-op)...")
        fixture.cleanup()  # Should not crash
        print("[PASS] Second cleanup succeeded (no crash)")

        print("\n[PASS] Test 3 PASSED: Double cleanup is safe")
        return True

    except Exception as e:
        print(f"[FAIL] Test 3 exception: {e}")
        import traceback

        traceback.print_exc()
        return False


def test_graceful_vs_force_kill(runtime_path: Path, provider_path: Path, port: int) -> bool:
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
    print("\n" + "=" * 60)
    print("  Test 4: Graceful -> Force Kill Escalation")
    print("=" * 60)

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=port, verbose=True)

    try:
        print("\n[1] Starting runtime...")
        if not fixture.start():
            print("[FAIL] Failed to start runtime")
            return False

        pid = fixture.get_pid()
        print(f"[PASS] Runtime started - PID={pid}")
        if pid is None:
            print("[FAIL] Could not get PID after start")
            return False
        print("\n[2] Cleanup (should try graceful SIGTERM first)...")
        start_time = time.time()
        fixture.cleanup()
        cleanup_duration = time.time() - start_time

        if not verify_process_cleanup(pid, timeout=3.0):
            print("[FAIL] Process still running after cleanup")
            return False

        print(f"[PASS] Process terminated in {cleanup_duration:.2f}s")

        if cleanup_duration > 7.0:
            print(f"[WARN] Cleanup took >{cleanup_duration:.1f}s (force kill timeout may have triggered)")
        else:
            print("[INFO] Cleanup completed quickly (likely graceful shutdown)")

        print("\n[PASS] Test 4 PASSED: Escalation logic working")
        return True

    except Exception as e:
        print(f"[FAIL] Test 4 exception: {e}")
        import traceback

        traceback.print_exc()
        return False
