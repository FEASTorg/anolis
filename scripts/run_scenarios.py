#!/usr/bin/env python3
"""
Scenario Runner for Anolis Validation

Orchestrates runtime startup, scenario execution, and result reporting.

Usage:
    python scripts/run_scenarios.py [OPTIONS]
    
Options:
    --runtime PATH       Path to anolis-runtime executable (default: auto-detect)
    --provider PATH      Path to anolis-provider-sim executable (default: auto-detect)
    --port PORT          HTTP server port (default: 8080)
    --scenario NAME      Run only specific scenario (default: all)
    --list               List available scenarios and exit
    --verbose            Show detailed output
    
Examples:
    # Run all scenarios
    python scripts/run_scenarios.py
    
    # Run specific scenario
    python scripts/run_scenarios.py --scenario happy_path_end_to_end
    
    # List scenarios
    python scripts/run_scenarios.py --list
"""

import argparse
import importlib
import inspect
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import List, Optional, Dict, Any
from dataclasses import dataclass

# Add scenarios to path for scenario imports
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent
SCENARIOS_DIR = PROJECT_ROOT / "scenarios"
sys.path.insert(0, str(PROJECT_ROOT))

from scenarios.base import ScenarioBase, ScenarioResult


@dataclass
class RuntimeProcess:
    """Wrapper for runtime and provider processes"""
    runtime_proc: subprocess.Popen
    provider_proc: subprocess.Popen
    base_url: str
    config_file: str
    

class ScenarioRunner:
    """Orchestrates runtime startup and scenario execution"""
    
    def __init__(self, runtime_path: str, provider_path: str, port: int, verbose: bool = False):
        self.runtime_path = runtime_path
        self.provider_path = provider_path
        self.port = port
        self.verbose = verbose
        self.base_url = f"http://localhost:{port}"
        
    def start_runtime(self) -> RuntimeProcess:
        """
        Start anolis runtime and provider-sim.
        
        Returns:
            RuntimeProcess with process handles
            
        Raises:
            RuntimeError if startup fails
        """
        # Create temporary config file
        config = {
            "http": {
                "enabled": True,
                "port": self.port,
                "host": "127.0.0.1"
            },
            "providers": [
                {
                    "id": "sim0",
                    "type": "stdio",
                    "command": self.provider_path,
                    "args": []
                }
            ],
            "control": {
                "default_mode": "MANUAL"
            }
        }
        
        # Create temp file for config
        fd, config_path = tempfile.mkstemp(suffix=".yaml", text=True)
        try:
            import yaml
        except ImportError:
            # Fallback to JSON if yaml not available
            with os.fdopen(fd, 'w') as f:
                json.dump(config, f, indent=2)
        else:
            with os.fdopen(fd, 'w') as f:
                yaml.dump(config, f)
                
        if self.verbose:
            print(f"[RUNNER] Starting runtime with config: {config_path}")
            print(f"[RUNNER] Runtime: {self.runtime_path}")
            print(f"[RUNNER] Provider: {self.provider_path}")
            
        # Start runtime
        try:
            runtime_proc = subprocess.Popen(
                [self.runtime_path, "--config", config_path],
                stdout=subprocess.PIPE if not self.verbose else None,
                stderr=subprocess.PIPE if not self.verbose else None,
                text=True
            )
        except Exception as e:
            os.unlink(config_path)
            raise RuntimeError(f"Failed to start runtime: {e}")
            
        # Wait for runtime to be ready
        if not self._wait_for_runtime(timeout=10.0):
            runtime_proc.kill()
            runtime_proc.wait()
            os.unlink(config_path)
            raise RuntimeError("Runtime failed to become ready")
            
        if self.verbose:
            print(f"[RUNNER] Runtime ready at {self.base_url}")
            
        # Note: provider is started by runtime, not directly by us
        return RuntimeProcess(
            runtime_proc=runtime_proc,
            provider_proc=None,  # Managed by runtime
            base_url=self.base_url,
            config_file=config_path
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
            
    def _wait_for_runtime(self, timeout: float = 10.0) -> bool:
        """Wait for runtime to respond to health checks."""
        import requests
        
        start = time.time()
        while time.time() - start < timeout:
            try:
                resp = requests.get(f"{self.base_url}/v0/runtime/status", timeout=1)
                if resp.status_code == 200:
                    return True
            except Exception:
                pass
            time.sleep(0.2)
        return False
        
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
                for name, obj in inspect.getmembers(module, inspect.isclass):
                    if (issubclass(obj, ScenarioBase) and 
                        obj is not ScenarioBase and
                        obj.__module__ == module.__name__):
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
                details=str(e)
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
            scenario_classes = [
                sc for sc in scenario_classes 
                if sc.__name__ == scenario_filter or sc.__name__.lower() == scenario_filter.lower()
            ]
            if not scenario_classes:
                print(f"ERROR: Scenario '{scenario_filter}' not found")
                return []
                
        if not scenario_classes:
            print("No scenarios found")
            return []
            
        # Start runtime
        print(f"Starting runtime and provider...")
        runtime = self.start_runtime()
        
        results = []
        
        try:
            # Run each scenario
            for scenario_class in scenario_classes:
                result = self.run_scenario(scenario_class, runtime)
                results.append(result)
                
                # Print immediate feedback
                status = "✓ PASS" if result.passed else "✗ FAIL"
                print(f"  {status} {result.name} ({result.duration_seconds:.2f}s)")
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
        
        print("\n" + "="*60)
        print(f"Scenario Results: {passed} passed, {failed} failed ({total_time:.2f}s total)")
        print("="*60)
        
        if failed > 0:
            print("\nFailed scenarios:")
            for r in results:
                if not r.passed:
                    print(f"  ✗ {r.name}")
                    print(f"      {r.message}")
                    if r.details:
                        print(f"      {r.details}")
                        
        print()


def find_executable(name: str, build_dir: str = "build") -> Optional[str]:
    """
    Auto-detect executable path.
    
    Args:
        name: Executable name (without extension)
        build_dir: Build directory name
        
    Returns:
        Path to executable or None if not found
    """
    # Try Release build first, then Debug
    for config in ["Release", "Debug"]:
        # Windows
        path = PROJECT_ROOT / build_dir / config / f"{name}.exe"
        if path.exists():
            return str(path)
            
        # Linux/Mac
        path = PROJECT_ROOT / build_dir / name
        if path.exists():
            return str(path)
            
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Run Anolis validation scenarios",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument(
        "--runtime",
        help="Path to anolis-runtime executable (default: auto-detect)"
    )
    parser.add_argument(
        "--provider",
        help="Path to anolis-provider-sim executable (default: auto-detect)"
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8080,
        help="HTTP server port (default: 8080)"
    )
    parser.add_argument(
        "--scenario",
        help="Run only specific scenario (default: all)"
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List available scenarios and exit"
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show detailed output"
    )
    
    args = parser.parse_args()
    
    # Auto-detect executables if not provided
    runtime_path = args.runtime
    if not runtime_path:
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
        
    # Create runner
    runner = ScenarioRunner(
        runtime_path=runtime_path,
        provider_path=provider_path,
        port=args.port,
        verbose=args.verbose
    )
    
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
    results = runner.run_all_scenarios(scenario_filter=args.scenario)
    
    # Print summary
    runner.print_summary(results)
    
    # Exit with appropriate code
    if not results:
        return 1
    failed = sum(1 for r in results if not r.passed)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
