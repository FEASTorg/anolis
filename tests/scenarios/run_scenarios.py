#!/usr/bin/env python3
"""
Scenario Runner for Anolis Validation

Orchestrates runtime startup, scenario execution, and result reporting.

Usage:
    python tests/scenarios/run_scenarios.py [OPTIONS]

Options:
    --runtime PATH       Path to anolis-runtime executable (default: auto-detect)
    --provider PATH      Path to anolis-provider-sim executable (default: auto-detect)
    --port PORT          HTTP server port (default: 8080)
    --scenario NAME      Run only specific scenario (default: all)
    --list               List available scenarios and exit
    --verbose            Show detailed output

Examples:
    # Run all scenarios
    python tests/scenarios/run_scenarios.py

    # Run specific scenario
    python tests/scenarios/run_scenarios.py --scenario happy_path_end_to_end

    # List scenarios
    python tests/scenarios/run_scenarios.py --list
"""

import argparse
import importlib
import inspect
import json
import os
import random
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import List, Optional

try:
    import psutil

    PSUTIL_AVAILABLE = True
except ImportError:
    PSUTIL_AVAILABLE = False

# NOTE: On Windows, use PYTHONIOENCODING=utf-8 environment variable if Unicode output issues occur.
# Monkey-patching sys.stdout/stderr here causes "I/O operation on closed file" errors in some environments.

# Add scenarios to path for scenario imports
# This allows 'import scenarios.base' to work by treating 'tests' as the package root
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent.parent
SCENARIOS_DIR = SCRIPT_DIR
sys.path.insert(0, str(SCRIPT_DIR.parent))

from scenarios.base import ScenarioBase, ScenarioResult  # noqa: E402


class StreamReader:
    """Reads stream in a background thread to prevent blocking."""

    def __init__(self, stream, name: str):
        self.stream = stream
        self.name = name
        self.output = []
        self.thread = threading.Thread(target=self._read_loop, daemon=True)
        self.thread.start()

    def _read_loop(self):
        try:
            for line in iter(self.stream.readline, ""):
                if not line:
                    break
                self.output.append(line)
        except Exception:
            pass

    def get_output(self) -> str:
        return "".join(self.output)


@dataclass
class RuntimeProcess:
    """Wrapper for runtime and provider processes"""

    runtime_proc: subprocess.Popen
    provider_proc: subprocess.Popen
    base_url: str
    config_file: str
    stdout_reader: Optional[StreamReader] = None
    stderr_reader: Optional[StreamReader] = None


class ScenarioRunner:
    """Orchestrates runtime startup and scenario execution"""

    def __init__(self, runtime_path: str, provider_path: str, port: int, verbose: bool = False):
        self.runtime_path = runtime_path
        self.provider_path = provider_path
        self.port = port
        self.verbose = verbose
        self.base_url = f"http://localhost:{port}"

    def start_runtime(self, policy="BLOCK") -> RuntimeProcess:
        """
        Start anolis runtime and provider-sim.

        Returns:
            RuntimeProcess with process handles

        Raises:
            RuntimeError if startup fails
        """
        # Find the test_noop.xml behavior tree file
        # Need absolute path since temp config file is in a different directory
        bt_path = PROJECT_ROOT / "behaviors" / "test_noop.xml"
        if not bt_path.exists():
            raise RuntimeError(f"Test behavior tree not found: {bt_path}")

        # Find provider config fixture
        provider_config_path = PROJECT_ROOT / "tests" / "integration" / "fixtures" / "provider-sim-default.yaml"
        if not provider_config_path.exists():
            raise RuntimeError(f"Provider config fixture not found: {provider_config_path}")

        # Create temporary config file
        config = {
            "runtime": {"mode": "MANUAL"},
            "http": {"enabled": True, "port": self.port, "bind": "127.0.0.1"},
            "providers": [
                {
                    "id": "sim0",
                    "command": self.provider_path,
                    "args": ["--config", str(provider_config_path).replace("\\", "/")],
                }
            ],
            "polling": {"interval_ms": 500},
            "logging": {"level": "info"},
            "automation": {
                "enabled": True,
                "behavior_tree": str(bt_path),
                "tick_rate_hz": 10,
                "manual_gating_policy": policy,
            },
        }

        # Create temp file for config
        fd, config_path = tempfile.mkstemp(suffix=".yaml", text=True)
        try:
            import yaml
        except ImportError:
            # Fallback to JSON if yaml not available
            with os.fdopen(fd, "w") as f:
                json.dump(config, f, indent=2)
        else:
            with os.fdopen(fd, "w") as f:
                yaml.dump(config, f)

        if self.verbose:
            print(f"[RUNNER] Starting runtime with config: {config_path}")
            print(f"[RUNNER] Runtime: {self.runtime_path}")
            print(f"[RUNNER] Provider: {self.provider_path}")

        if self.verbose:
            # Always print basic diagnostic info for CI debugging
            print(f"[DEBUG] Runtime executable: {self.runtime_path}")
            print(f"[DEBUG] Runtime exists: {os.path.exists(self.runtime_path)}")
            print(f"[DEBUG] Provider executable: {self.provider_path}")
            print(f"[DEBUG] Provider exists: {os.path.exists(self.provider_path)}")

        # Start runtime
        try:
            runtime_proc = subprocess.Popen(
                [self.runtime_path, "--config", config_path],
                stdout=subprocess.PIPE if not self.verbose else None,
                stderr=subprocess.PIPE if not self.verbose else None,
                text=True,
                bufsize=1,  # Line buffered
            )
        except Exception as e:
            os.unlink(config_path)
            raise RuntimeError(f"Failed to start runtime: {e}") from e

        stdout_reader = None
        stderr_reader = None

        if not self.verbose:
            if runtime_proc.stdout:
                stdout_reader = StreamReader(runtime_proc.stdout, "stdout")
            if runtime_proc.stderr:
                stderr_reader = StreamReader(runtime_proc.stderr, "stderr")

        if self.verbose:
            print(f"[DEBUG] Runtime process started, PID: {runtime_proc.pid}")

        # Wait for runtime to be ready
        ready, wait_message = self._wait_for_runtime(timeout=10.0)
        if not ready:
            # Capture stderr/stdout for diagnostics
            runtime_proc.kill()
            runtime_proc.wait()

            stdout_out = stdout_reader.get_output() if stdout_reader else ""
            stderr_out = stderr_reader.get_output() if stderr_reader else ""

            os.unlink(config_path)

            print(f"[DEBUG] Runtime failed to become ready: {wait_message}")
            if stdout_out:
                print(f"[DEBUG] Runtime stdout:\n{stdout_out}")
            if stderr_out:
                print(f"[DEBUG] Runtime stderr:\n{stderr_out}")

            raise RuntimeError(f"Runtime failed to become ready: {wait_message}")
        if self.verbose:
            print(f"[DEBUG] Runtime ready at {self.base_url}")

        # Note: provider is started by runtime, not directly by us
        return RuntimeProcess(
            runtime_proc=runtime_proc,
            provider_proc=None,  # Managed by runtime
            base_url=self.base_url,
            config_file=config_path,
            stdout_reader=stdout_reader,
            stderr_reader=stderr_reader,
        )

    def stop_runtime(self, runtime: RuntimeProcess):
        """Stop runtime and provider, clean up resources."""
        if self.verbose:
            print("[RUNNER] Stopping runtime...")

        if runtime.runtime_proc:
            runtime.runtime_proc.terminate()
            try:
                runtime.runtime_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                runtime.runtime_proc.kill()
                runtime.runtime_proc.wait()

        # Clean up config file
        try:
            if os.path.exists(runtime.config_file):
                os.unlink(runtime.config_file)
        except Exception:
            pass

    def _wait_for_runtime(self, timeout: float = 10.0) -> tuple:
        """
        Wait for runtime to respond to health checks.

        Returns:
            Tuple of (success: bool, message: str)
        """
        import requests

        last_error = None
        attempts = 0
        start = time.time()
        while time.time() - start < timeout:
            attempts += 1
            try:
                resp = requests.get(f"{self.base_url}/v0/runtime/status", timeout=1)
                if resp.status_code == 200:
                    return True, f"Ready after {attempts} attempts"
                else:
                    last_error = f"HTTP {resp.status_code}: {resp.text[:200]}"
            except requests.exceptions.ConnectionError:
                last_error = f"Connection refused (attempt {attempts})"
            except requests.exceptions.Timeout:
                last_error = f"Request timeout (attempt {attempts})"
            except Exception as e:
                last_error = f"{type(e).__name__}: {e}"
            time.sleep(0.2)
        return (
            False,
            f"Timeout after {timeout}s, {attempts} attempts. Last error: {last_error}",
        )

    def discover_scenarios(self) -> List[type]:
        """
        Discover all scenario classes in scenarios directory.

        Returns:
            List of scenario class types
        """
        scenarios = []

        # Scan scenarios directory for .py files
        for py_file in SCENARIOS_DIR.glob("*.py"):
            if py_file.name.startswith("_"):
                continue  # Skip __init__.py and private files

            module_name = py_file.stem

            try:
                # Import module
                module = importlib.import_module(f"scenarios.{module_name}")

                # Find ScenarioBase subclasses
                for _name, obj in inspect.getmembers(module, inspect.isclass):
                    if issubclass(obj, ScenarioBase) and obj is not ScenarioBase and obj.__module__ == module.__name__:
                        scenarios.append(obj)

            except Exception as e:
                if self.verbose:
                    print(f"[RUNNER] Warning: Failed to import {module_name}: {e}")

        return scenarios

    def run_scenario(self, scenario_class: type, runtime: RuntimeProcess) -> ScenarioResult:
        """
        Run a single scenario.

        Args:
            scenario_class: Scenario class to instantiate and run
            runtime: RuntimeProcess with running runtime

        Returns:
            ScenarioResult with pass/fail status
        """
        scenario = scenario_class(runtime.base_url)
        scenario.start_time = time.time()

        if self.verbose:
            print(f"[RUNNER] Running scenario: {scenario.name}")

        try:
            # Setup
            scenario.setup()

            # Run
            result = scenario.run()

            # Cleanup
            try:
                scenario.cleanup()
            except Exception as cleanup_err:
                if self.verbose:
                    print(f"[RUNNER] Cleanup error: {cleanup_err}")

            return result

        except Exception as e:
            # Scenario failed with exception
            duration = time.time() - scenario.start_time

            # Try cleanup anyway
            try:
                scenario.cleanup()
            except Exception:
                pass

            return ScenarioResult(
                name=scenario.name,
                passed=False,
                duration_seconds=duration,
                message=f"Exception: {type(e).__name__}",
                details=str(e),
            )

    def run_all_scenarios(self, scenario_filter: Optional[str] = None) -> List[ScenarioResult]:
        """
        Run all discovered scenarios (or filtered subset).

        Args:
            scenario_filter: If provided, only run scenario with this name

        Returns:
            List of ScenarioResult for all scenarios
        """
        # Discover scenarios
        scenario_classes = self.discover_scenarios()

        if scenario_filter:
            # Normalize filter and class names for robust matching
            # e.g. "happy_path_end_to_end" matches "HappyPathEndToEnd"
            normalized_filter = scenario_filter.lower().replace("_", "")

            scenario_classes = [
                sc
                for sc in scenario_classes
                if sc.__name__.lower() == normalized_filter or sc.__name__.lower().replace("_", "") == normalized_filter
            ]
            if not scenario_classes:
                print(f"ERROR: Scenario '{scenario_filter}' not found")
                return []

        if not scenario_classes:
            print("No scenarios found")
            return []
        results = []
        total_scenarios = len(scenario_classes)

        # Run each scenario
        for i, scenario_class in enumerate(scenario_classes, 1):
            # Determine configuration based on scenario
            policy = "BLOCK"
            if scenario_class.__name__ == "OverridePolicy":
                policy = "OVERRIDE"

            print(
                f"[{i}/{total_scenarios}] Running {scenario_class.__name__}...",
                end="",
                flush=True,
            )

            # Start runtime for this scenario
            if self.verbose:
                print(f"\n[RUNNER] Starting runtime for {scenario_class.__name__} (policy={policy})...")

            try:
                runtime = self.start_runtime(policy=policy)
            except Exception as e:
                print(f"\n  [FAIL] {scenario_class.__name__} (startup failed)")
                print(f"      {str(e)}")
                results.append(
                    ScenarioResult(
                        scenario_class.__name__,
                        False,
                        0.0,
                        "Runtime startup failed",
                        str(e),
                    )
                )
                continue

            try:
                result = self.run_scenario(scenario_class, runtime)
                results.append(result)

                # Print immediate feedback
                status = "PASS" if result.passed else "FAIL"
                # Overwrite the line or just append
                # Since we used end="", we are on the same line.
                print(f" {status} ({result.duration_seconds:.2f}s)")

                status_mark = "[PASS]" if result.passed else "[FAIL]"
                print(f"  {status_mark} {result.name} ({result.duration_seconds:.2f}s)")
                if not result.passed:
                    print(f"      {result.message}")
                    if result.details and self.verbose:
                        print(f"      Details: {result.details}")
            finally:
                # Stop runtime
                self.stop_runtime(runtime)

        return results

    def print_summary(self, results: List[ScenarioResult]):
        """Print summary of scenario results."""
        if not results:
            return

        passed = sum(1 for r in results if r.passed)
        failed = len(results) - passed
        total_time = sum(r.duration_seconds for r in results)

        print("\n" + "=" * 60)
        print(f"Scenario Results: {passed} passed, {failed} failed ({total_time:.2f}s total)")
        print("=" * 60)

        if failed > 0:
            print("\nFailed scenarios:")
            for r in results:
                if not r.passed:
                    print(f"  [FAIL] {r.name}")
                    print(f"      {r.message}")
                    if r.details:
                        print(f"      {r.details}")

        print()

    def generate_json_report(self, results: List[ScenarioResult], start_time: float, end_time: float, output_path: str):
        """Generate JSON report from scenario results."""
        report = {
            "start_time": datetime.fromtimestamp(start_time).isoformat() + "Z",
            "end_time": datetime.fromtimestamp(end_time).isoformat() + "Z",
            "duration_seconds": round(end_time - start_time, 2),
            "runtime_path": self.runtime_path,
            "provider_path": self.provider_path,
            "port": self.port,
            "scenarios": [
                {
                    "name": r.name,
                    "status": "PASS" if r.passed else "FAIL",
                    "duration_seconds": round(r.duration_seconds, 2),
                    "message": r.message or "",
                    "details": r.details or "",
                }
                for r in results
            ],
            "summary": {
                "total": len(results),
                "passed": sum(1 for r in results if r.passed),
                "failed": sum(1 for r in results if not r.passed),
                "skipped": 0,
                "pass_rate": round(sum(1 for r in results if r.passed) / len(results), 2) if results else 0.0,
            },
        }

        try:
            with open(output_path, "w") as f:
                json.dump(report, f, indent=2)
            print(f"JSON report written to: {output_path}")
        except Exception as e:
            print(f"ERROR: Failed to write JSON report: {e}")

    def run_soak_test(self, duration_seconds: int, report_path: Optional[str] = None):
        """Run soak test with continuous operation and monitoring."""
        if not PSUTIL_AVAILABLE:
            print("WARNING: psutil not available, memory/thread monitoring disabled")
            print("Install with: pip install psutil")

        print("\n" + "=" * 60)
        print(f"Starting Soak Test ({duration_seconds}s / {duration_seconds // 60}min)")
        print("=" * 60)

        # Start runtime in AUTO mode
        print("\n[SOAK] Starting runtime in AUTO mode...")
        try:
            runtime = self.start_runtime(policy="BLOCK")
        except Exception as e:
            print(f"[SOAK] Failed to start runtime: {e}")
            return 1

        # Get process object for monitoring
        runtime_proc = psutil.Process(runtime.runtime_proc.pid) if PSUTIL_AVAILABLE else None

        # Switch to AUTO mode
        try:
            import requests

            resp = requests.post(f"{runtime.base_url}/v0/mode", json={"mode": "AUTO"}, timeout=5)
            if resp.status_code == 200:
                print("[SOAK] Runtime in AUTO mode")
            else:
                print(f"[SOAK] WARNING: Failed to set AUTO mode: {resp.status_code}")
        except Exception as e:
            print(f"[SOAK] WARNING: Failed to set AUTO mode: {e}")

        # Monitoring state
        start_time = time.time()
        next_report_time = start_time + 300  # Report every 5 minutes
        initial_rss = runtime_proc.memory_info().rss if runtime_proc else 0
        initial_threads = runtime_proc.num_threads() if runtime_proc else 0
        peak_rss = initial_rss
        fault_injections = 0
        parameter_updates = 0

        print(f"[SOAK] Initial memory: {initial_rss / 1024 / 1024:.2f} MB")
        print(f"[SOAK] Initial threads: {initial_threads}")
        print("[SOAK] Running continuous operations...\n")

        try:
            while time.time() - start_time < duration_seconds:
                elapsed = time.time() - start_time

                # Every 10s: Update a random parameter
                if int(elapsed) % 10 == 0 and int(elapsed) > 0:
                    try:
                        import requests

                        # Try to update temp_setpoint to random value
                        value = random.uniform(20.0, 30.0)
                        resp = requests.post(
                            f"{runtime.base_url}/v0/parameters",
                            json={"name": "temp_setpoint", "value": value},
                            timeout=2,
                        )
                        if resp.status_code == 200:
                            parameter_updates += 1
                    except Exception:
                        pass

                # Every 30s: Inject a random fault
                if int(elapsed) % 30 == 0 and int(elapsed) > 0:
                    try:
                        import requests

                        fault_type = random.choice(["UNAVAILABLE", "SIGNAL_FAULT", "CALL_LATENCY"])
                        args = {"fault_type": fault_type}
                        if fault_type == "CALL_LATENCY":
                            args["latency_ms"] = random.randint(100, 500)
                        resp = requests.post(
                            f"{runtime.base_url}/v0/call",
                            json={
                                "provider": "sim0",
                                "device": "sim_control",
                                "function": "inject_fault",
                                "args": args,
                            },
                            timeout=2,
                        )
                        if resp.status_code == 200:
                            fault_injections += 1
                            # Clear fault after 5 seconds
                            time.sleep(5)
                            requests.post(
                                f"{runtime.base_url}/v0/call",
                                json={
                                    "provider": "sim0",
                                    "device": "sim_control",
                                    "function": "clear_fault",
                                    "args": {},
                                },
                                timeout=2,
                            )
                    except Exception:
                        pass

                # Monitor and report every 5 minutes
                if time.time() >= next_report_time:
                    if runtime_proc:
                        current_rss = runtime_proc.memory_info().rss
                        current_threads = runtime_proc.num_threads()
                        peak_rss = max(peak_rss, current_rss)
                        memory_growth = ((current_rss - initial_rss) / initial_rss * 100) if initial_rss > 0 else 0

                        print(f"[SOAK] {int(elapsed)}s elapsed:")
                        print(f"       Memory: {current_rss / 1024 / 1024:.2f} MB (growth: {memory_growth:+.1f}%)")
                        print(f"       Threads: {current_threads}")
                        print(f"       Faults injected: {fault_injections}")
                        print(f"       Parameters updated: {parameter_updates}")
                    next_report_time += 300

                time.sleep(1)

        finally:
            # Final report
            end_time = time.time()
            duration = end_time - start_time
            exit_code = 0  # Default exit code

            if runtime_proc:
                final_rss = runtime_proc.memory_info().rss
                final_threads = runtime_proc.num_threads()
                memory_growth = ((final_rss - initial_rss) / initial_rss * 100) if initial_rss > 0 else 0

                print("\n" + "=" * 60)
                print(f"Soak Test Complete ({duration:.1f}s)")
                print("=" * 60)
                print(f"Initial memory: {initial_rss / 1024 / 1024:.2f} MB")
                print(f"Final memory:   {final_rss / 1024 / 1024:.2f} MB")
                print(f"Peak memory:    {peak_rss / 1024 / 1024:.2f} MB")
                print(f"Memory growth:  {memory_growth:+.1f}%")
                print(f"Initial threads: {initial_threads}")
                print(f"Final threads:   {final_threads}")
                print(f"Fault injections: {fault_injections}")
                print(f"Parameter updates: {parameter_updates}")

                # Determine pass/fail
                passed = memory_growth < 10.0 and abs(final_threads - initial_threads) <= 2
                status = "PASS" if passed else "FAIL"
                print(f"\n{status}: Memory growth {'<' if passed else '>='} 10%, threads stable")

                # Generate JSON report if requested
                if report_path:
                    report = {
                        "test_type": "soak",
                        "start_time": datetime.fromtimestamp(start_time).isoformat() + "Z",
                        "end_time": datetime.fromtimestamp(end_time).isoformat() + "Z",
                        "duration_seconds": round(duration, 2),
                        "runtime_path": self.runtime_path,
                        "provider_path": self.provider_path,
                        "memory": {
                            "initial_bytes": initial_rss,
                            "final_bytes": final_rss,
                            "peak_bytes": peak_rss,
                            "growth_percent": round(memory_growth, 2),
                        },
                        "threads": {
                            "initial": initial_threads,
                            "final": final_threads,
                        },
                        "operations": {
                            "fault_injections": fault_injections,
                            "parameter_updates": parameter_updates,
                        },
                        "status": status,
                        "passed": passed,
                    }
                    try:
                        with open(report_path, "w") as f:
                            json.dump(report, f, indent=2)
                        print(f"JSON report written to: {report_path}")
                    except Exception as e:
                        print(f"ERROR: Failed to write JSON report: {e}")

                self.stop_runtime(runtime)
                exit_code = 0 if passed else 1
            else:
                print("\n[SOAK] Monitoring unavailable (psutil not installed)")
                self.stop_runtime(runtime)
                exit_code = 0

        return exit_code


def find_executable(name: str, build_dir: str = "build") -> Optional[str]:
    """
    Auto-detect executable path.

    Args:
        name: Executable name (without extension)
        build_dir: Build directory name

    Returns:
        Path to executable or None if not found
    """
    # Search locations (relative to PROJECT_ROOT)
    # We check both root 'build/' and 'build/core/' (where cmake puts binaries)
    search_dirs = [
        Path(build_dir),
        Path(build_dir) / "core",
    ]

    for d in search_dirs:
        # Try Release build first, then Debug
        for config in ["Release", "Debug", "."]:
            # Windows
            path = PROJECT_ROOT / d / config / f"{name}.exe"
            if path.exists():
                return str(path)

            # Linux/Mac
            path = PROJECT_ROOT / d / config / name
            if path.exists():
                return str(path)

            # Linux/Mac (no config folder)
            path = PROJECT_ROOT / d / name
            if path.exists():
                return str(path)

    return None

    return None


def main():
    parser = argparse.ArgumentParser(
        description="Run Anolis validation scenarios",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    parser.add_argument("--runtime", help="Path to anolis-runtime executable (default: auto-detect)")
    parser.add_argument(
        "--provider",
        help="Path to anolis-provider-sim executable (default: auto-detect)",
    )
    parser.add_argument("--port", type=int, default=8080, help="HTTP server port (default: 8080)")
    parser.add_argument("--scenario", help="Run only specific scenario (default: all)")
    parser.add_argument("--list", action="store_true", help="List available scenarios and exit")
    parser.add_argument("--verbose", action="store_true", help="Show detailed output")
    parser.add_argument("--report", help="Output JSON report to specified file")
    parser.add_argument("--soak", action="store_true", help="Run soak test (extended stability test)")
    parser.add_argument("--duration", type=int, default=1800, help="Soak test duration in seconds (default: 1800)")
    parser.add_argument("--start-only", action="store_true", help="Start runtime and exit (leave running)")
    parser.add_argument("--stop", action="store_true", help="Stop running runtime and exit")

    args = parser.parse_args()

    # Auto-detect executables if not provided
    runtime_path = args.runtime
    if not runtime_path:
        # Check core/ build location
        runtime_path = find_executable("anolis-runtime", "build/core")
        if not runtime_path:
            # Check root build location (fallback)
            runtime_path = find_executable("anolis-runtime", "build")

        if not runtime_path:
            print("ERROR: Could not find anolis-runtime executable")
            print("Use --runtime to specify path")
            return 1

    provider_path = args.provider
    if not provider_path:
        provider_path = find_executable("anolis-provider-sim", "../anolis-provider-sim/build")
        if not provider_path:
            print("ERROR: Could not find anolis-provider-sim executable")
            print("Use --provider to specify path")
            return 1

    if args.verbose:
        print(f"Runtime: {runtime_path}")
        print(f"Provider: {provider_path}")

    # Handle --stop flag (stop running runtime)
    if args.stop:
        pid_file = Path(tempfile.gettempdir()) / "anolis-runtime.pid"
        if not pid_file.exists():
            print("No running runtime found (PID file not found)")
            return 1

        try:
            with open(pid_file, "r") as f:
                pid = int(f.read().strip())

            if PSUTIL_AVAILABLE:
                proc = psutil.Process(pid)
                proc.terminate()
                proc.wait(timeout=10)
                print(f"Runtime stopped (PID {pid})")
            else:
                import signal

                os.kill(pid, signal.SIGTERM)
                print(f"Sent SIGTERM to runtime (PID {pid})")

            pid_file.unlink()
            return 0
        except (ProcessLookupError, psutil.NoSuchProcess):
            print("Runtime process not found (already stopped?)")
            pid_file.unlink()
            return 1
        except Exception as e:
            print(f"Failed to stop runtime: {e}")
            return 1

    # Create runner
    runner = ScenarioRunner(
        runtime_path=runtime_path,
        provider_path=provider_path,
        port=args.port,
        verbose=args.verbose,
    )

    # Handle --start-only flag (start runtime and exit, leave running)
    if args.start_only:
        print("Starting runtime (will leave running)...")
        try:
            runtime = runner.start_runtime(policy="BLOCK")

            # Write PID file for --stop
            pid_file = Path(tempfile.gettempdir()) / "anolis-runtime.pid"
            with open(pid_file, "w") as f:
                f.write(str(runtime.runtime_proc.pid))

            print(f"Runtime started successfully (PID {runtime.runtime_proc.pid})")
            print(f"Base URL: {runtime.base_url}")
            print(f"\nTo stop: python {Path(__file__).name} --stop")
            return 0
        except Exception as e:
            print(f"Failed to start runtime: {e}")
            return 1

    # Handle --soak flag (run soak test)
    if args.soak:
        return runner.run_soak_test(args.duration, args.report)

    # List scenarios if requested
    if args.list:
        scenarios = runner.discover_scenarios()
        if not scenarios:
            print("No scenarios found")
            return 0
        print(f"Found {len(scenarios)} scenarios:")
        for sc in scenarios:
            print(f"  - {sc.__name__}")
        return 0

    # Run scenarios
    start_time = time.time()
    results = runner.run_all_scenarios(scenario_filter=args.scenario)
    end_time = time.time()

    # Print summary
    runner.print_summary(results)

    # Generate JSON report if requested
    if args.report:
        runner.generate_json_report(results, start_time, end_time, args.report)

    # Exit with appropriate code
    if not results:
        return 1
    failed = sum(1 for r in results if not r.passed)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
