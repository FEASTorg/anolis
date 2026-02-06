# Validation Scenario Suite

Comprehensive test scenarios validating anolis runtime behavior against the provider-sim dry-run machine.

## Overview

This suite provides **deterministic, repeatable tests** covering core workflows, edge cases, fault handling, and concurrency. All scenarios run against a live anolis runtime connected to provider-sim.

## Running Scenarios

### Run all scenarios

```bash
python scripts/run_scenarios.py
```

### Run specific scenario

```bash
python scripts/run_scenarios.py --scenario HappyPathEndToEnd
```

### List available scenarios

```bash
python scripts/run_scenarios.py --list
```

### Verbose output

```bash
python scripts/run_scenarios.py --verbose
```

## Scenario Catalog

### Infrastructure

- **test_infrastructure.py**: Validates scenario runner infrastructure (baseline connectivity test)

### Core Workflows

- **happy_path_end_to_end.py**: Full workflow validation from device discovery through state change verification
- **mode_blocking_policy.py**: Validates AUTO mode blocks manual calls when policy is BLOCK
- **override_policy.py**: Validates manual calls succeed in AUTO mode with OVERRIDE policy
- **precondition_enforcement.py**: Validates precondition checking blocks invalid calls
- **parameter_validation.py**: Validates parameter type, range, and requirement checking

### Fault Handling & Recovery

- **fault_to_manual_recovery.py**: Device fault triggers FAULT mode, manual recovery restores operation
- **provider_restart_recovery.py**: Runtime recovers gracefully when provider becomes unavailable

### Observability & Performance

- **telemetry_on_change.py**: Validates telemetry only fires on actual signal changes (optimization check)
- **slow_sse_client_behavior.py**: Validates runtime responsiveness under high load with slow consumers
- **multi_device_concurrency.py**: Validates concurrent multi-device operations without deadlocks

## Writing Scenarios

All scenarios inherit from `ScenarioBase` which provides:

### HTTP API Helpers

```python
# Device discovery
devices = self.get_devices()
capabilities = self.get_capabilities(provider_id, device_id)

# State reading
state = self.get_state(provider_id, device_id)
mode = self.get_mode()

# Control
result = self.call_function(provider_id, device_id, function_id, args)
self.set_mode(mode)

# Parameters
params = self.get_parameters()
self.set_parameters(params)
```

### Assertion Helpers

```python
# Basic assertions
self.assert_equal(expected, actual, context)
self.assert_true(condition, message)

# Device-specific
self.assert_device_exists(provider_id, device_id)
self.assert_signal_value(provider_id, device_id, signal_id, expected_value)
self.assert_signal_quality(provider_id, device_id, signal_id, expected_quality)

# Mode assertions
self.assert_mode(expected_mode)
```

### Polling Utilities

```python
# Wait for condition
self.poll_until(
    condition_fn=lambda: self.get_mode() == "MANUAL",
    timeout_sec=5.0,
    poll_interval_sec=0.1,
    error_message="Mode did not transition to MANUAL"
)

# Wait for mode
self.wait_for_mode(expected_mode, timeout_sec=5.0)
```

### Fault Injection

```python
# Inject faults via sim_control device
self.call_function("sim0", "sim_control", "inject_device_unavailable", {
    "device_id": "tempctl0",
    "duration_ms": 5000
})

# Clear all faults
self.call_function("sim0", "sim_control", "clear_faults", {})
```

## Scenario Template

```python
from .base import ScenarioBase, ScenarioResult

class MyScenario(ScenarioBase):
    """Brief description of what this scenario validates."""

    def setup(self) -> None:
        """One-time setup before scenario runs."""
        super().setup()  # Important: clears faults and resets mode
        # Add scenario-specific setup

    def run(self) -> ScenarioResult:
        """Execute test steps and return result."""
        try:
            # Step 1: Setup test conditions
            # Step 2: Perform actions
            # Step 3: Verify outcomes

            return ScenarioResult(
                passed=True,
                message="Scenario passed successfully"
            )
        except AssertionError as e:
            return ScenarioResult(
                passed=False,
                message=f"Assertion failed: {e}"
            )
        except Exception as e:
            return ScenarioResult(
                passed=False,
                message=f"Unexpected error: {e}"
            )

    def cleanup(self) -> None:
        """Cleanup after scenario completes."""
        # Add scenario-specific cleanup
        super().cleanup()  # Important: clears faults and resets mode
```

## Design Principles

1. **Idempotent**: Each scenario starts with clean state and cleans up after itself
2. **Deterministic**: No race conditions, no random failures
3. **Self-contained**: Scenarios don't depend on execution order
4. **Clear diagnostics**: Assertion failures include context for debugging
5. **Fast**: Scenarios complete in seconds, not minutes

## CI Integration

Scenarios run automatically in CI via:

```yaml
- name: Run Validation Scenarios
  run: python scripts/run_scenarios.py
```

All scenarios must pass before merging.

## Troubleshooting

### Scenario fails intermittently

- Check for polling timeouts (increase `timeout_sec` in `poll_until`)
- Verify runtime/provider are fully started before scenario begins
- Check for timing assumptions (use polling instead of fixed sleeps)

### Runtime connection refused

- Ensure runtime is started before running scenarios
- Check runtime is listening on expected port (default 8080)
- Verify no firewall blocking localhost connections

### Provider not responding

- Ensure provider-sim is running and connected to runtime
- Check provider logs for errors
- Verify fault injection was cleared after previous scenario

### Assertion failure unclear

- Run with `--verbose` flag for detailed HTTP request/response logs
- Add diagnostic logging to scenario
- Use `assert_equal` with context string for better error messages
