#!/usr/bin/env python3
"""
Anolis Test Suite Runner

Runs all integration tests sequentially with proper cleanup between tests.
This is the main entry point for CI and local test runs.

Usage:
    python scripts/test_all.py [--verbose] [--timeout SECONDS]

Exit Codes:
    0 - All tests passed
    1 - One or more tests failed
    2 - Test infrastructure error
"""

import subprocess
import sys
import os
import time
import signal
import argparse
from pathlib import Path
from dataclasses import dataclass
from typing import List, Optional


@dataclass
class TestSuiteResult:
    name: str
    passed: bool
    duration: float
    message: str = ""


def get_script_dir() -> Path:
    """Get the directory containing this script."""
    return Path(__file__).parent.resolve()


def get_repo_root() -> Path:
    """Get the repository root directory."""
    return get_script_dir().parent


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


def find_provider_path() -> Optional[Path]:
    """Find the provider-sim executable."""
    repo_root = get_repo_root()

    # Check multiple locations:
    # 1. Sibling repo (local dev setup)
    # 2. Inside workspace (CI setup)
    candidates = [
        # Sibling repo (local dev)
        repo_root.parent
        / "anolis-provider-sim"
        / "build"
        / "Release"
        / "anolis-provider-sim.exe",
        repo_root.parent
        / "anolis-provider-sim"
        / "build"
        / "Release"
        / "anolis-provider-sim",
        repo_root.parent
        / "anolis-provider-sim"
        / "build"
        / "Debug"
        / "anolis-provider-sim.exe",
        repo_root.parent
        / "anolis-provider-sim"
        / "build"
        / "Debug"
        / "anolis-provider-sim",
        repo_root.parent / "anolis-provider-sim" / "build" / "anolis-provider-sim.exe",
        repo_root.parent / "anolis-provider-sim" / "build" / "anolis-provider-sim",
        # Inside workspace (CI)
        repo_root
        / "anolis-provider-sim"
        / "build"
        / "Release"
        / "anolis-provider-sim.exe",
        repo_root / "anolis-provider-sim" / "build" / "Release" / "anolis-provider-sim",
        repo_root / "anolis-provider-sim" / "build" / "anolis-provider-sim.exe",
        repo_root / "anolis-provider-sim" / "build" / "anolis-provider-sim",
    ]

    for path in candidates:
        if path.exists():
            return path

    return None


def kill_processes(names: List[str]) -> None:
    """Kill any running processes by name."""
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
        for name in names:
            try:
                subprocess.run(
                    ["pkill", "-9", "-f", name], capture_output=True, timeout=5
                )
            except Exception:
                pass


def cleanup_between_tests() -> None:
    """Clean up any lingering processes between tests."""
    print("  Cleaning up processes...")
    kill_processes(["anolis-runtime", "anolis-provider-sim"])
    # Give OS time to release resources
    time.sleep(1)


def run_test_script(
    script_name: str,
    runtime_path: Path,
    provider_path: Path,
    timeout: int,
    verbose: bool,
    extra_args: List[str] = None,
) -> TestSuiteResult:
    """Run a single test script and capture results."""
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

    start_time = time.time()

    try:
        result = subprocess.run(
            cmd,
            capture_output=not verbose,
            text=True,
            timeout=timeout + 30,  # Extra buffer for test overhead
            cwd=get_repo_root(),
        )

        duration = time.time() - start_time

        if result.returncode == 0:
            return TestSuiteResult(
                name=script_name,
                passed=True,
                duration=duration,
                message="All tests passed",
            )
        else:
            error_msg = (
                result.stderr
                if result.stderr
                else result.stdout if result.stdout else "Unknown error"
            )
            # Truncate long error messages
            if len(error_msg) > 500:
                error_msg = error_msg[:500] + "..."
            return TestSuiteResult(
                name=script_name,
                passed=False,
                duration=duration,
                message=f"Exit code {result.returncode}: {error_msg}",
            )

    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        return TestSuiteResult(
            name=script_name,
            passed=False,
            duration=duration,
            message=f"Timeout after {timeout + 30}s (subprocess timeout)",
        )
    except Exception as e:
        duration = time.time() - start_time
        return TestSuiteResult(
            name=script_name, passed=False, duration=duration, message=f"Exception: {e}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run all Anolis integration tests")
    parser.add_argument("--runtime", type=str, help="Path to anolis-runtime executable")
    parser.add_argument(
        "--provider", type=str, help="Path to anolis-provider-sim executable"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=60,
        help="Timeout per test suite in seconds (default: 60)",
    )
    parser.add_argument(
        "--verbose", action="store_true", help="Show test output in real-time"
    )
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
    ]

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

        status = "[PASS]" if result.passed else "[FAIL]"
        print(f"{status} {script_name} ({result.duration:.1f}s)")
        if not result.passed:
            print(f"  Error: {result.message}")
        print()

        # Cleanup between tests
        cleanup_between_tests()

    # Summary
    print("=" * 60)
    print("Summary")
    print("=" * 60)

    passed = sum(1 for r in results if r.passed)
    failed = len(results) - passed
    total_duration = sum(r.duration for r in results)

    for result in results:
        status = "[PASS]" if result.passed else "[FAIL]"
        print(f"  {status} {result.name}")

    print()
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
