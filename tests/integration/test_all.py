#!/usr/bin/env python3
"""
Anolis Test Suite Runner

Runs all integration tests sequentially with proper cleanup between tests.
This is the main entry point for CI and local test runs.

Usage:
    python tests/integration/test_all.py [--verbose] [--timeout SECONDS]

Exit Codes:
    0 - All tests passed
    1 - One or more tests failed
    2 - Test infrastructure error
"""

import argparse
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

# Allow skipping stress tests via environment variable
SKIP_STRESS_TESTS = os.environ.get("SKIP_STRESS_TESTS", "").lower() in ("1", "true", "yes")


@dataclass
class TestSuiteResult:
    name: str
    passed: bool
    duration: float
    message: str = ""
    skipped: bool = False


def get_script_dir() -> Path:
    """Get the directory containing this script."""
    return Path(__file__).parent.resolve()


def get_repo_root() -> Path:
    """Get the repository root directory."""
    # From tests/integration/test_all.py -> tests/integration -> tests -> root
    return get_script_dir().parent.parent


def find_runtime_path() -> Optional[Path]:
    """Find the runtime executable."""
    repo_root = get_repo_root()

    # Check common build locations
    candidates = [
        repo_root / "build" / "core" / "Release" / "anolis-runtime.exe",
        repo_root / "build" / "core" / "Release" / "anolis-runtime",
        repo_root / "build" / "core" / "Debug" / "anolis-runtime.exe",
        repo_root / "build" / "core" / "Debug" / "anolis-runtime",
        repo_root / "build" / "core" / "anolis-runtime.exe",
        repo_root / "build" / "core" / "anolis-runtime",
    ]

    for path in candidates:
        if path.exists():
            return path

    return None


def find_bt_nodes_sanity_path(runtime_path: Optional[Path] = None) -> Optional[Path]:
    """Find the bt_nodes_sanity executable if it exists."""
    repo_root = get_repo_root()

    # If runtime path provided, derive build directory from it
    build_dirs = []
    if runtime_path:
        # runtime is typically in build*/core/[Config]/anolis-runtime
        # or build*/core/anolis-runtime
        build_dir = runtime_path.parent
        if build_dir.name in ["Release", "Debug", "RelWithDebInfo", "MinSizeRel"]:
            build_dir = build_dir.parent.parent  # up to build directory
        elif build_dir.name == "core":
            build_dir = build_dir.parent  # up to build directory
        build_dirs.append(build_dir)

    # Fallback to common build directories
    build_dirs.extend([repo_root / "build-tsan", repo_root / "build"])

    candidates = []
    for build_dir in build_dirs:
        candidates.extend([
            build_dir / "core" / "Release" / "bt_nodes_sanity.exe",
            build_dir / "core" / "Release" / "bt_nodes_sanity",
            build_dir / "core" / "Debug" / "bt_nodes_sanity.exe",
            build_dir / "core" / "Debug" / "bt_nodes_sanity",
            build_dir / "core" / "bt_nodes_sanity.exe",
            build_dir / "core" / "bt_nodes_sanity",
        ])

    for candidate in candidates:
        if candidate.exists():
            return candidate

    return None


def find_provider_path() -> Optional[Path]:
    """Find the provider-sim executable."""
    repo_root = get_repo_root()

    # Check multiple locations:
    # 1. Sibling repo (local dev setup)
    # 2. Inside workspace (CI setup)
    candidates = [
        # Sibling repo (local dev)
        repo_root.parent / "anolis-provider-sim" / "build" / "Release" / "anolis-provider-sim.exe",
        repo_root.parent / "anolis-provider-sim" / "build" / "Release" / "anolis-provider-sim",
        repo_root.parent / "anolis-provider-sim" / "build" / "Debug" / "anolis-provider-sim.exe",
        repo_root.parent / "anolis-provider-sim" / "build" / "Debug" / "anolis-provider-sim",
        repo_root.parent / "anolis-provider-sim" / "build" / "anolis-provider-sim.exe",
        repo_root.parent / "anolis-provider-sim" / "build" / "anolis-provider-sim",
        # Inside workspace (CI)
        repo_root / "anolis-provider-sim" / "build" / "Release" / "anolis-provider-sim.exe",
        repo_root / "anolis-provider-sim" / "build" / "Release" / "anolis-provider-sim",
        repo_root / "anolis-provider-sim" / "build" / "anolis-provider-sim.exe",
        repo_root / "anolis-provider-sim" / "build" / "anolis-provider-sim",
    ]

    for path in candidates:
        if path.exists():
            return path

    return None


def kill_processes(names: List[str]) -> None:
    """
    Emergency cleanup for leaked processes (safety net only).

    NOTE: As of Sprint 3.2, all tests use RuntimeFixture with process-group
    scoped cleanup. This function is kept as a safety net for:
    - Test failures that prevent normal cleanup
    - Manual interruption (Ctrl+C)
    - Unexpected crashes

    Normal test execution should NOT rely on this function.
    """
    if sys.platform == "win32":
        for name in names:
            try:
                subprocess.run(
                    ["taskkill", "/F", "/IM", f"{name}.exe"],
                    capture_output=True,
                    timeout=5,
                )
            except Exception:
                pass
    else:
        # On Linux, use pgrep/pkill with exact binary name match (-x flag)
        # to avoid killing Python processes or matching directory paths
        for name in names:
            try:
                # First try graceful SIGTERM with exact name match
                subprocess.run(["pkill", "-x", name], capture_output=True, timeout=5)
            except Exception:
                pass
        # Brief delay to let processes exit gracefully
        time.sleep(0.5)
        # Then force kill any remaining
        for name in names:
            try:
                subprocess.run(["pkill", "-9", "-x", name], capture_output=True, timeout=5)
            except Exception:
                pass


def get_log_dir() -> Path:
    """Get or create the test logs directory."""
    log_dir = get_repo_root() / "build" / "test-logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    return log_dir


def cleanup_between_tests() -> None:
    """
    Emergency cleanup between tests (safety net only).

    NOTE: Individual tests now use RuntimeFixture with proper cleanup.
    This function is kept as a failsafe for interrupted or crashed tests.
    """
    print("  Cleaning up processes...")
    kill_processes(["anolis-runtime", "anolis-provider-sim"])
    # Give OS time to release resources
    time.sleep(1)


def read_tail_lines(file_path: Path, num_lines: int = 50) -> str:
    """Read the last N lines of a file."""
    try:
        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
            tail = lines[-num_lines:] if len(lines) > num_lines else lines
            return "".join(tail)
    except Exception as e:
        return f"(Could not read log: {e})"


def run_test_script(
    script_name: str,
    runtime_path: Path,
    provider_path: Path,
    timeout: int,
    verbose: bool,
    extra_args: List[str] = None,
) -> TestSuiteResult:
    """Run a single test script, streaming output to log file to prevent OOM."""
    script_path = get_script_dir() / script_name

    if not script_path.exists():
        return TestSuiteResult(
            name=script_name,
            passed=False,
            duration=0,
            message=f"Script not found: {script_path}",
        )

    cmd = [
        sys.executable,
        str(script_path),
        f"--runtime={runtime_path}",
        f"--provider={provider_path}",
        f"--timeout={timeout}",
    ]

    if extra_args:
        cmd.extend(extra_args)

    # Create log file for this test
    log_dir = get_log_dir()
    log_file_path = log_dir / f"{script_name}.log"

    start_time = time.time()

    try:
        # Stream output to log file instead of buffering in memory
        # This prevents OOM on Linux CI when tests produce lots of output
        with open(log_file_path, "w", encoding="utf-8") as log_file:
            if verbose:
                # In verbose mode, tee to both file and stdout
                process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    cwd=get_repo_root(),
                )

                # Read and write line by line
                while True:
                    line = process.stdout.readline()
                    if not line and process.poll() is not None:
                        break
                    if line:
                        print(line, end="", flush=True)
                        log_file.write(line)
                        log_file.flush()

                result_code = process.wait(timeout=timeout + 30)
            else:
                # Non-verbose: just stream to file
                result = subprocess.run(
                    cmd,
                    stdout=log_file,
                    stderr=subprocess.STDOUT,
                    timeout=timeout + 30,
                    cwd=get_repo_root(),
                )
                result_code = result.returncode

        duration = time.time() - start_time

        if result_code == 0:
            # Check if test was skipped by looking for skip marker in log
            # This allows any test to signal skip by printing "SKIPPING:" to stdout
            try:
                with open(log_file_path, "r", encoding="utf-8", errors="replace") as f:
                    log_content = f.read()
                    
                    # Look for skip markers (case-insensitive, supports multiple patterns)
                    skip_markers = ["SKIPPING:", "SKIP:", "TEST SKIPPED"]
                    found_skip = any(marker in log_content.upper() for marker in skip_markers)
                    
                    if found_skip:
                        # Extract skip reason - look for lines with skip info
                        lines = log_content.split("\n")
                        skip_info = []
                        
                        # Find the skip section and extract relevant lines
                        in_skip_section = False
                        for line in lines:
                            line_upper = line.upper()
                            # Start of skip section
                            if any(marker in line_upper for marker in skip_markers):
                                in_skip_section = True
                                # Extract the test name if present
                                for marker in skip_markers:
                                    if marker in line_upper:
                                        skip_info.append(line.split(marker)[1].strip() if marker in line else line.strip())
                                        break
                            # In skip section - collect reason lines
                            elif in_skip_section:
                                if line.startswith("Reason:") or line.startswith("  "):
                                    skip_info.append(line.strip())
                                elif line.strip() and not line.strip().startswith("="):
                                    skip_info.append(line.strip())
                                # End of skip section when hitting another border or empty lines
                                elif line.strip().startswith("=") or (not line.strip() and skip_info):
                                    break
                        
                        # Build skip message
                        skip_msg = " - ".join(skip_info[:3]) if skip_info else "Test skipped"
                        
                        return TestSuiteResult(
                            name=script_name,
                            passed=True,
                            duration=duration,
                            message=skip_msg,
                            skipped=True,
                        )
            except Exception:
                pass  # If we can't read log, treat as regular pass
            
            return TestSuiteResult(
                name=script_name,
                passed=True,
                duration=duration,
                message="All tests passed",
            )
        else:
            # Read last 50 lines of log for error context
            error_tail = read_tail_lines(log_file_path, 50)
            # Truncate if still too long
            if len(error_tail) > 2000:
                error_tail = error_tail[-2000:]
            return TestSuiteResult(
                name=script_name,
                passed=False,
                duration=duration,
                message=f"Exit code {result_code}. Log tail:\n{error_tail}",
            )

    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        error_tail = read_tail_lines(log_file_path, 30)
        return TestSuiteResult(
            name=script_name,
            passed=False,
            duration=duration,
            message=f"Timeout after {timeout + 30}s. Log tail:\n{error_tail}",
        )
    except Exception as e:
        duration = time.time() - start_time
        return TestSuiteResult(name=script_name, passed=False, duration=duration, message=f"Exception: {e}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run all Anolis integration tests")
    parser.add_argument("--runtime", type=str, help="Path to anolis-runtime executable")
    parser.add_argument("--provider", type=str, help="Path to anolis-provider-sim executable")
    parser.add_argument(
        "--timeout",
        type=int,
        default=60,
        help="Timeout per test suite in seconds (default: 60)",
    )
    parser.add_argument("--verbose", action="store_true", help="Show test output in real-time")
    parser.add_argument(
        "--port",
        type=int,
        default=8080,
        help="HTTP port for HTTP tests (default: 8080)",
    )

    args = parser.parse_args()

    # Find executables
    runtime_path = Path(args.runtime) if args.runtime else find_runtime_path()
    provider_path = Path(args.provider) if args.provider else find_provider_path()

    if not runtime_path or not runtime_path.exists():
        print("ERROR: Runtime executable not found.")
        print("Build with: cmake --build build --config Release")
        if runtime_path:
            print(f"  Checked: {runtime_path}")
        return 2

    if not provider_path or not provider_path.exists():
        print("ERROR: Provider executable not found.")
        print("Build anolis-provider-sim first.")
        if provider_path:
            print(f"  Checked: {provider_path}")
        return 2

    print("=" * 60)
    print("Anolis Test Suite")
    print("=" * 60)
    print(f"Runtime:  {runtime_path}")
    print(f"Provider: {provider_path}")
    print(f"Timeout:  {args.timeout}s per suite")
    print(f"Port:     {args.port}")
    print("=" * 60)
    print()

    # Define test suites to run (in order)
    test_suites = [
        ("test_core.py", []),
        ("test_http.py", [f"--port={args.port}"]),
        ("test_automation.py", [f"--port={args.port + 1}"]),
        ("test_signal_handling.py", []),
        ("test_provider_supervision.py", []),
    ]

    # Add stress test unless explicitly skipped
    if not SKIP_STRESS_TESTS:
        test_suites.append(("test_concurrency_stress.py", ["--stress-level=light", f"--port={args.port + 2}"]))
        print("Note: Concurrency stress test uses 'light' mode for CI speed")
        print("      Run manually with --stress-level=moderate or =heavy for deeper testing")
        print()
    else:
        print("Note: Skipping stress tests (SKIP_STRESS_TESTS=1)")
        print()

    bt_nodes_sanity = find_bt_nodes_sanity_path(runtime_path)

    results: List[TestSuiteResult] = []

    # Initial cleanup
    cleanup_between_tests()

    for script_name, extra_args in test_suites:
        print(f"Running: {script_name}")
        print("-" * 40)

        result = run_test_script(
            script_name=script_name,
            runtime_path=runtime_path,
            provider_path=provider_path,
            timeout=args.timeout,
            verbose=args.verbose,
            extra_args=extra_args,
        )

        results.append(result)

        if result.skipped:
            status = "[SKIP]"
            print(f"{status} {script_name} ({result.duration:.1f}s)")
            if result.message:
                print(f"  {result.message}")
        elif result.passed:
            status = "[PASS]"
            print(f"{status} {script_name} ({result.duration:.1f}s)")
        else:
            status = "[FAIL]"
            print(f"{status} {script_name} ({result.duration:.1f}s)")
            if result.message:
                print(f"  Error: {result.message}")

        # Initial cleanup
        cleanup_between_tests()

    # Run BT node sanity test if available
    if bt_nodes_sanity:
        print("Running: bt_nodes_sanity")
        print("-" * 40)
        start = time.time()
        try:
            proc = subprocess.run(
                [str(bt_nodes_sanity)],
                timeout=args.timeout,
                capture_output=not args.verbose,
                text=True,
            )
            duration = time.time() - start
            passed = proc.returncode == 0
            message = "" if passed else (proc.stderr.strip() or proc.stdout.strip())
            results.append(TestSuiteResult("bt_nodes_sanity", passed, duration, message))
            status = "[PASS]" if passed else "[FAIL]"
            print(f"{status} bt_nodes_sanity ({duration:.1f}s)")
            if not passed:
                print(f"  Error: {message}")
        except Exception as e:
            duration = time.time() - start
            results.append(TestSuiteResult("bt_nodes_sanity", False, duration, str(e)))
            print(f"[FAIL] bt_nodes_sanity ({duration:.1f}s)")
            print(f"  Error: {e}")
        print()

        # Cleanup between tests
        cleanup_between_tests()

    # Summary
    print("=" * 60)
    print("Summary")
    print("=" * 60)

    passed = sum(1 for r in results if r.passed and not r.skipped)
    skipped = sum(1 for r in results if r.skipped)
    failed = sum(1 for r in results if not r.passed)
    total_duration = sum(r.duration for r in results)

    for result in results:
        if result.skipped:
            status = "[SKIP]"
            print(f"  {status} {result.name}")
            if result.message:
                print(f"         {result.message}")
        elif result.passed:
            status = "[PASS]"
            print(f"  {status} {result.name}")
        else:
            status = "[FAIL]"
            print(f"  {status} {result.name}")

    print()
    if skipped > 0:
        print(f"Passed: {passed}/{len(results)}, Skipped: {skipped}")
    else:
        print(f"Passed: {passed}/{len(results)}")
    print(f"Duration: {total_duration:.1f}s")

    if failed == 0:
        print()
        print("[PASS] ALL TESTS PASSED")
        return 0
    else:
        print()
        print(f"[FAIL] {failed} test suite(s) failed")
        return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        cleanup_between_tests()
        sys.exit(130)
