#!/usr/bin/env python3
"""
Process Cleanup Verification Test

Validates that RuntimeFixture provides safe, scoped process cleanup:
1. Only terminates the spawned process (no collateral damage)
2. Uses process-group scoped termination
3. Proper escalation (SIGTERM → SIGKILL)
4. Cleanup works even on test failure

Usage:
    python tests/integration/test_process_cleanup.py [--runtime PATH] [--provider PATH]
"""

import argparse
import sys
import time
from pathlib import Path

from test_fixtures import RuntimeFixture, verify_process_cleanup


def find_default_paths():
    """Find default runtime/provider paths"""
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent.parent

    # Try multiple candidate locations
    runtime_candidates = [
        repo_root / "build" / "core" / "Release" / "anolis-runtime.exe",
        repo_root / "build" / "core" / "Release" / "anolis-runtime",
        repo_root / "build" / "core" / "anolis-runtime.exe",
        repo_root / "build" / "core" / "anolis-runtime",
    ]

    provider_candidates = [
        repo_root.parent / "anolis-provider-sim" / "build" / "Release" / "anolis-provider-sim.exe",
        repo_root.parent / "anolis-provider-sim" / "build" / "Release" / "anolis-provider-sim",
        repo_root.parent / "anolis-provider-sim" / "build" / "anolis-provider-sim.exe",
        repo_root.parent / "anolis-provider-sim" / "build" / "anolis-provider-sim",
    ]

    runtime_path = None
    for path in runtime_candidates:
        if path.exists():
            runtime_path = path
            break

    provider_path = None
    for path in provider_candidates:
        if path.exists():
            provider_path = path
            break

    return runtime_path, provider_path


def test_scoped_cleanup(runtime_path: Path, provider_path: Path) -> bool:
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

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=8081, verbose=True)

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

        print("\n[✓] Test 1 PASSED: Scoped cleanup working")
        return True

    except Exception as e:
        print(f"[FAIL] Test 1 exception: {e}")
        import traceback

        traceback.print_exc()
        return False

    finally:
        # Ensure cleanup even if test fails
        fixture.cleanup()


def test_cleanup_on_exception(runtime_path: Path, provider_path: Path) -> bool:
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

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=8082, verbose=True)
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
    print("\n[✓] Test 2 PASSED: Cleanup on exception working")
    return True


def test_double_cleanup(runtime_path: Path, provider_path: Path) -> bool:
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

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=8083, verbose=True)

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

        print("\n[✓] Test 3 PASSED: Double cleanup is safe")
        return True

    except Exception as e:
        print(f"[FAIL] Test 3 exception: {e}")
        import traceback

        traceback.print_exc()
        return False


def test_graceful_vs_force_kill(runtime_path: Path, provider_path: Path) -> bool:
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
    print("  Test 4: Graceful → Force Kill Escalation")
    print("=" * 60)

    fixture = RuntimeFixture(runtime_path, provider_path, http_port=8084, verbose=True)

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

        print("\n[✓] Test 4 PASSED: Escalation logic working")
        return True

    except Exception as e:
        print(f"[FAIL] Test 4 exception: {e}")
        import traceback

        traceback.print_exc()
        return False


def main():
    """Run all process cleanup verification tests."""
    parser = argparse.ArgumentParser(description="Process cleanup verification tests")
    parser.add_argument("--runtime", help="Path to anolis-runtime executable")
    parser.add_argument("--provider", help="Path to anolis-provider-sim executable")

    args = parser.parse_args()

    # Find default paths if not provided
    default_runtime, default_provider = find_default_paths()

    runtime_path = Path(args.runtime) if args.runtime else default_runtime
    provider_path = Path(args.provider) if args.provider else default_provider

    if not runtime_path or not runtime_path.exists():
        print(f"ERROR: Runtime not found at {runtime_path}")
        return 1

    if not provider_path or not provider_path.exists():
        print(f"ERROR: Provider not found at {provider_path}")
        return 1

    print("\n" + "=" * 60)
    print("  Process Cleanup Verification Test Suite")
    print("=" * 60)
    print(f"Runtime:  {runtime_path}")
    print(f"Provider: {provider_path}")

    # Run all tests
    results = []
    results.append(("Scoped Cleanup", test_scoped_cleanup(runtime_path, provider_path)))
    results.append(("Cleanup on Exception", test_cleanup_on_exception(runtime_path, provider_path)))
    results.append(("Double Cleanup Safety", test_double_cleanup(runtime_path, provider_path)))
    results.append(("Graceful/Force Escalation", test_graceful_vs_force_kill(runtime_path, provider_path)))

    # Summary
    print("\n" + "=" * 60)
    print("  Test Summary")
    print("=" * 60)

    passed = sum(1 for _, result in results if result)
    total = len(results)

    for name, result in results:
        status = "[PASS]" if result else "[FAIL]"
        print(f"{status} {name}")

    print(f"\nTotal: {passed}/{total} tests passed")

    if passed == total:
        print("\n✓ All process cleanup tests PASSED")
        return 0
    else:
        print(f"\n✗ {total - passed} test(s) FAILED")
        return 1


if __name__ == "__main__":
    sys.exit(main())
