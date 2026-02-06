"""
Scenario Registry and Discovery

Imports all scenario modules for discovery by the runner.
"""

# Import all scenarios for discovery
from . import happy_path_end_to_end
from . import mode_blocking_policy
from . import override_policy
from . import precondition_enforcement
from . import parameter_validation
from . import fault_to_manual_recovery
from . import telemetry_on_change
from . import slow_sse_client_behavior
from . import multi_device_concurrency
from . import provider_restart_recovery
from . import test_infrastructure

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
