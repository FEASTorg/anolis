"""
Scenario Registry and Discovery

Imports all scenario modules for discovery by the runner.
"""

# Import all scenarios for discovery
from . import (
    fault_to_manual_recovery,
    happy_path_end_to_end,
    mode_blocking_policy,
    multi_device_concurrency,
    override_policy,
    parameter_validation,
    precondition_enforcement,
    provider_restart_recovery,
    slow_sse_client_behavior,
    telemetry_on_change,
    test_infrastructure,
)

__all__ = [
    "happy_path_end_to_end",
    "mode_blocking_policy",
    "override_policy",
    "precondition_enforcement",
    "parameter_validation",
    "fault_to_manual_recovery",
    "telemetry_on_change",
    "slow_sse_client_behavior",
    "multi_device_concurrency",
    "provider_restart_recovery",
    "test_infrastructure",
]
