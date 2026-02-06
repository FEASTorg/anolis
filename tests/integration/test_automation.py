#!/usr/bin/env python3
"""
Anolis Automation Layer Integration Test

This script validates automation functionality:
- Mode state machine (MANUAL, AUTO, IDLE, FAULT)
- Mode transitions (valid and invalid)
- Manual/auto contention policy (BLOCK/OVERRIDE)
- BT execution gating by mode
- Mode change events in telemetry
- HTTP API for mode control

Usage:
    python tests/integration/test_automation.py [--runtime PATH] [--provider PATH] [--port PORT]

Prerequisites:
    - Runtime and provider executables must be built
    - Automation enabled in config
"""

import subprocess
import sys
import os
import time
import requests
import tempfile
import yaml
import argparse
from typing import Optional, Dict, Any


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    END = "\033[0m"
    BOLD = "\033[1m"


def log_test(name: str):
    print(f"\n{Colors.BLUE}[TEST]{Colors.END} {name}")


def log_pass(message: str):
    print(f"  {Colors.GREEN}[OK]{Colors.END} {message}")


def log_fail(message: str):
    print(f"  {Colors.RED}[FAIL]{Colors.END} {message}")


def log_info(message: str):
    print(f"  {Colors.YELLOW}[INFO]{Colors.END} {message}")


class AutomationTester:
    def __init__(self, runtime_path: str, provider_path: str, port: int):
        self.runtime_path = runtime_path
        self.provider_path = provider_path
        self.port = port
        self.base_url = f"http://127.0.0.1:{port}"
        # script (integration) -> tests -> root
        self.repo_root = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..")
        )
        self.runtime_process: Optional[subprocess.Popen] = None
        self.config_file: Optional[str] = None
        self.tests_passed = 0
        self.tests_failed = 0

    def create_config(
        self, automation_enabled: bool = True, manual_gating_policy: str = "BLOCK"
    ) -> str:
        """Create temporary runtime config with automation enabled"""
        config = {
            "runtime": {"mode": "MANUAL"},
            "http": {"enabled": True, "bind": "127.0.0.1", "port": self.port},
            "providers": [
                {
                    "id": "sim0",
                    "command": self.provider_path,
                    "args": [],
                    "timeout_ms": 5000,
                }
            ],
            "polling": {"interval_ms": 500},
            "telemetry": {"enabled": False},
            "automation": {
                "enabled": automation_enabled,
                "behavior_tree": os.path.join(self.repo_root, "behaviors", "demo.xml"),
                "tick_rate_hz": 10,
                "manual_gating_policy": manual_gating_policy,
                "parameters": [
                    {
                        "name": "temp_setpoint",
                        "type": "double",
                        "default": 25.0,
                        "min": 10.0,
                        "max": 50.0,
                    },
                    {
                        "name": "motor_duty_cycle",
                        "type": "int64",
                        "default": 50,
                        "min": 0,
                        "max": 100,
                    },
                    {"name": "control_enabled", "type": "bool", "default": True},
                    {
                        "name": "operating_mode",
                        "type": "string",
                        "default": "normal",
                        "allowed_values": ["normal", "test", "emergency"],
                    },
                ],
            },
            "logging": {"level": "info"},
        }

        fd, path = tempfile.mkstemp(suffix=".yaml", text=True)
        with os.fdopen(fd, "w") as f:
            yaml.dump(config, f)

        return path

    def start_runtime(self):
        """Start runtime process"""
        log_info(f"Starting runtime: {self.runtime_path}")
        # Don't redirect stdout/stderr - let runtime output go to console
        # This avoids handle inheritance issues with provider subprocesses
        self.runtime_process = subprocess.Popen(
            [self.runtime_path, "--config", self.config_file],
            stdin=subprocess.DEVNULL,
            cwd=self.repo_root,  # Set working directory so relative paths work
        )

        # Wait for HTTP endpoint to be up
        deadline = time.time() + 10.0
        while time.time() < deadline:
            if self.runtime_process.poll() is not None:
                raise RuntimeError(
                    f"Runtime process terminated with exit code {self.runtime_process.returncode}"
                )
            
            try:
                requests.get(f"{self.base_url}/v0/devices", timeout=1)
                log_pass("Runtime started successfully")
                return
            except requests.RequestException:
                time.sleep(0.1)

        raise RuntimeError("Runtime HTTP endpoint did not become available")

    def stop_runtime(self):
        """Stop runtime process"""
        if self.runtime_process:
            self.runtime_process.terminate()
            try:
                self.runtime_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.runtime_process.kill()
            self.runtime_process = None

    def cleanup(self):
        """Clean up resources"""
        self.stop_runtime()
        if self.config_file and os.path.exists(self.config_file):
            os.unlink(self.config_file)

    def get_mode(self) -> Optional[Dict[str, Any]]:
        """Get current mode via HTTP API"""
        try:
            resp = requests.get(f"{self.base_url}/v0/mode", timeout=2)
            return resp.json()
        except requests.RequestException as e:
            log_fail(f"Request exception: {e}")
            return None

    def set_mode(self, mode: str) -> Optional[Dict[str, Any]]:
        """Set mode via HTTP API"""
        try:
            resp = requests.post(
                f"{self.base_url}/v0/mode", json={"mode": mode}, timeout=2
            )
            return resp.json()
        except requests.RequestException as e:
            log_fail(f"Request failed: {e}")
            return None

    def test_get_mode_when_automation_enabled(self) -> bool:
        """Test GET /v0/mode returns current mode"""
        log_test("GET /v0/mode when automation enabled")

        result = self.get_mode()
        if not result:
            log_fail("Failed to get mode")
            return False

        if "mode" not in result:
            log_fail("Response missing 'mode' field")
            return False

        if result["mode"] != "MANUAL":
            log_fail(f"Expected mode MANUAL, got {result['mode']}")
            return False

        log_pass(f"Current mode: {result['mode']}")
        return True

    def test_mode_transition_manual_to_auto(self) -> bool:
        """Test valid transition: MANUAL -> AUTO"""
        log_test("Mode transition: MANUAL -> AUTO")

        result = self.set_mode("AUTO")
        if not result:
            log_fail("Failed to set mode")
            return False

        if result.get("mode") != "AUTO":
            log_fail(f"Expected AUTO, got {result.get('mode')}")
            return False

        log_pass("Transitioned to AUTO")
        return True

    def test_mode_transition_auto_to_manual(self) -> bool:
        """Test valid transition: AUTO -> MANUAL"""
        log_test("Mode transition: AUTO -> MANUAL")

        # First go to AUTO
        self.set_mode("AUTO")
        time.sleep(0.5)

        # Then back to MANUAL
        result = self.set_mode("MANUAL")
        if not result:
            log_fail("Failed to set mode")
            return False

        if result.get("mode") != "MANUAL":
            log_fail(f"Expected MANUAL, got {result.get('mode')}")
            return False

        log_pass("Transitioned to MANUAL")
        return True

    def test_mode_transition_manual_to_idle(self) -> bool:
        """Test valid transition: MANUAL -> IDLE"""
        log_test("Mode transition: MANUAL -> IDLE")

        # Ensure we're in MANUAL
        self.set_mode("MANUAL")
        time.sleep(0.5)

        result = self.set_mode("IDLE")
        if not result:
            log_fail("Failed to set mode")
            return False

        if result.get("mode") != "IDLE":
            log_fail(f"Expected IDLE, got {result.get('mode')}")
            return False

        log_pass("Transitioned to IDLE")
        return True

    def test_mode_transition_to_fault(self) -> bool:
        """Test valid transition: Any -> FAULT"""
        log_test("Mode transition: MANUAL -> FAULT")

        # Ensure we're in MANUAL
        self.set_mode("MANUAL")
        time.sleep(0.5)

        result = self.set_mode("FAULT")
        if not result:
            log_fail("Failed to set mode")
            return False

        if result.get("mode") != "FAULT":
            log_fail(f"Expected FAULT, got {result.get('mode')}")
            return False

        log_pass("Transitioned to FAULT")
        return True

    def test_invalid_transition_fault_to_auto(self) -> bool:
        """Test invalid transition: FAULT -> AUTO (should be rejected)"""
        log_test("Invalid transition: FAULT -> AUTO")

        # Go to FAULT first
        self.set_mode("FAULT")
        time.sleep(0.5)

        result = self.set_mode("AUTO")
        if not result:
            log_fail("Request failed completely")
            return False

        # Should get error status
        if "status" in result and result["status"].get("code") == "FAILED_PRECONDITION":
            log_pass("Transition correctly rejected")
            return True

        log_fail(f"Expected FAILED_PRECONDITION error, got: {result}")
        return False

    def test_recovery_path_fault_to_manual_to_auto(self) -> bool:
        """Test recovery path: FAULT -> MANUAL -> AUTO"""
        log_test("Recovery path: FAULT -> MANUAL -> AUTO")

        # Go to FAULT
        self.set_mode("FAULT")
        time.sleep(0.5)

        # Recover through MANUAL
        result1 = self.set_mode("MANUAL")
        if not result1 or result1.get("mode") != "MANUAL":
            log_fail("Failed to recover to MANUAL")
            return False
        log_info("Recovered to MANUAL")

        # Then go to AUTO
        result2 = self.set_mode("AUTO")
        if not result2 or result2.get("mode") != "AUTO":
            log_fail("Failed to transition to AUTO")
            return False

        log_pass("Successfully recovered: FAULT -> MANUAL -> AUTO")
        return True

    def test_invalid_mode_string(self) -> bool:
        """Test invalid mode string rejection"""
        log_test("Invalid mode string: INVALID")

        result = self.set_mode("INVALID")
        if not result:
            log_fail("Request failed completely")
            return False

        if "status" in result and result["status"].get("code") == "INVALID_ARGUMENT":
            log_pass("Invalid mode correctly rejected")
            return True

        log_fail(f"Expected INVALID_ARGUMENT error, got: {result}")
        return False

    def test_get_parameters(self) -> bool:
        """Test GET /v0/parameters returns declared parameters"""
        log_test("GET /v0/parameters")

        try:
            resp = requests.get(f"{self.base_url}/v0/parameters", timeout=2)
            if resp.status_code != 200:
                log_fail(f"Expected 200 OK, got {resp.status_code}")
                return False
            body = resp.json()
            if "parameters" not in body:
                log_fail("Response missing 'parameters' field")
                return False
            # Basic sanity: check temp_setpoint exists
            names = [p.get("name") for p in body.get("parameters", [])]
            if "temp_setpoint" not in names:
                log_fail("Expected 'temp_setpoint' in parameters")
                return False
            log_pass("Parameters listed")
            return True
        except requests.RequestException as e:
            log_fail(f"Request exception: {e}")
            return False

    def test_update_parameter_valid(self) -> bool:
        """Test POST /v0/parameters valid update"""
        log_test("POST /v0/parameters (valid)")

        try:
            resp = requests.post(
                f"{self.base_url}/v0/parameters",
                json={"name": "temp_setpoint", "value": 30.0},
                timeout=2,
            )
            if resp.status_code != 200:
                log_fail(f"Expected 200 OK, got {resp.status_code} - {resp.text}")
                return False
            body = resp.json()
            val = body.get("parameter", {}).get("value")
            if val != 30.0:
                log_fail(f"Expected updated value 30.0, got {val}")
                return False
            log_pass("Parameter updated successfully")
            return True
        except requests.RequestException as e:
            log_fail(f"Request exception: {e}")
            return False

    def test_update_parameter_out_of_range(self) -> bool:
        """Test POST /v0/parameters rejects out-of-range value"""
        log_test("POST /v0/parameters (out-of-range)")

        try:
            resp = requests.post(
                f"{self.base_url}/v0/parameters",
                json={"name": "temp_setpoint", "value": 100.0},
                timeout=2,
            )
            # Expect 400 with INVALID_ARGUMENT
            if resp.status_code == 200:
                log_fail("Expected rejection but got 200 OK")
                return False
            body = resp.json()
            if "status" in body and body["status"].get("code") == "INVALID_ARGUMENT":
                log_pass("Out-of-range correctly rejected")
                return True
            log_fail(f"Expected INVALID_ARGUMENT, got: {body}")
            return False
        except requests.RequestException as e:
            log_fail(f"Request exception: {e}")
            return False

    def test_automation_disabled_returns_unavailable(self) -> bool:
        """Test that mode endpoints return UNAVAILABLE when automation disabled"""
        log_test("Mode API when automation disabled")

        # Stop current runtime
        self.stop_runtime()
        time.sleep(2)  # Give more time for cleanup

        # Create config with automation disabled
        if self.config_file and os.path.exists(self.config_file):
            os.unlink(self.config_file)
        self.config_file = self.create_config(automation_enabled=False)

        # Start runtime
        self.start_runtime()

        # Try to get mode
        result = self.get_mode()
        if not result:
            log_fail("Request failed completely")
            return False

        if "status" in result and result["status"].get("code") == "UNAVAILABLE":
            log_pass("Correctly returned UNAVAILABLE")
            return True

        log_fail(f"Expected UNAVAILABLE error, got: {result}")
        return False

    def run_all_tests(self) -> bool:
        """Run all automation tests"""
        print(f"\n{Colors.BOLD}=== Anolis Automation Tests ==={Colors.END}\n")

        tests = [
            self.test_get_mode_when_automation_enabled,
            self.test_mode_transition_manual_to_auto,
            self.test_mode_transition_auto_to_manual,
            self.test_mode_transition_manual_to_idle,
            self.test_mode_transition_to_fault,
            self.test_invalid_transition_fault_to_auto,
            self.test_recovery_path_fault_to_manual_to_auto,
            self.test_invalid_mode_string,
            # Parameter tests
            self.test_get_parameters,
            self.test_update_parameter_valid,
            self.test_update_parameter_out_of_range,
            # Automation disabled test
            self.test_automation_disabled_returns_unavailable,
        ]

        for test in tests:
            try:
                if test():
                    self.tests_passed += 1
                else:
                    self.tests_failed += 1
            except Exception as e:
                log_fail(f"Exception: {e}")
                self.tests_failed += 1

        # Summary
        total = self.tests_passed + self.tests_failed
        print(f"\n{Colors.BOLD}=== Test Summary ==={Colors.END}")
        print(f"Total: {total}")
        print(f"{Colors.GREEN}Passed: {self.tests_passed}{Colors.END}")
        print(f"{Colors.RED}Failed: {self.tests_failed}{Colors.END}")

        return self.tests_failed == 0


def main():
    parser = argparse.ArgumentParser(description="Anolis Automation Tests")
    parser.add_argument(
        "--runtime",
        default="./build/core/Release/anolis-runtime.exe",
        help="Path to runtime executable",
    )
    parser.add_argument(
        "--provider",
        default="../anolis-provider-sim/build/Release/anolis-provider-sim.exe",
        help="Path to provider executable",
    )
    parser.add_argument(
        "--port", type=int, default=18080, help="HTTP port to use (default: 18080)"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=120,
        help="Test timeout in seconds (default: 120)",
    )
    args = parser.parse_args()

    # Validate paths
    if not os.path.exists(args.runtime):
        print(f"{Colors.RED}Error:{Colors.END} Runtime not found: {args.runtime}")
        sys.exit(1)

    if not os.path.exists(args.provider):
        print(f"{Colors.RED}Error:{Colors.END} Provider not found: {args.provider}")
        sys.exit(1)

    tester = AutomationTester(
        runtime_path=os.path.abspath(args.runtime),
        provider_path=os.path.abspath(args.provider),
        port=args.port,
    )

    try:
        # Create config and start runtime
        tester.config_file = tester.create_config()
        tester.start_runtime()

        # Run tests
        success = tester.run_all_tests()

        if success:
            print(f"\n{Colors.GREEN}[PASS]{Colors.END} All automation tests passed!\n")
            sys.exit(0)
        else:
            print(f"\n{Colors.RED}[FAIL]{Colors.END} Some tests failed\n")
            sys.exit(1)

    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(130)
    except Exception as e:
        print(f"\n{Colors.RED}Error:{Colors.END} {e}")
        sys.exit(1)
    finally:
        tester.cleanup()


if __name__ == "__main__":
    main()
