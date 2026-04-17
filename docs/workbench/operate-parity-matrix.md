# Workbench Operate Parity Matrix (Phase 07)

Status: Closed for retirement-blocking parity checks.  
Last reviewed: 2026-04-16.

Purpose: Map legacy `tools/operator-ui` operator workflows to Workbench Operate
and identify whether any retirement-blocking capability remains exclusive to
legacy UI.

## Retirement-Blocking Capability Matrix

| Capability | Legacy Operator UI | Workbench Operate | Parity Status |
| --- | --- | --- | --- |
| Runtime status badge (`/v0/runtime/status`) | `tools/operator-ui/index.html`, `js/app.js` | `tools/workbench/frontend/index.html`, `js/operate-workspace.js` | Complete |
| SSE stream connection states (`connected`, `reconnecting`, `disconnected`, `stale`) | `tools/operator-ui/js/sse.js`, `js/app.js` | `tools/workbench/frontend/js/operate/events.js`, `js/operate-workspace.js` | Complete |
| Mode get/set (`GET/POST /v0/mode`) | `tools/operator-ui/js/automation.js` | `tools/workbench/frontend/js/operate-workspace.js` | Complete |
| Automation status (`/v0/automation/status`) | `tools/operator-ui/js/automation.js` | `tools/workbench/frontend/js/operate-workspace.js` | Complete |
| Behavior tree panel (`/v0/automation/tree`) | `tools/operator-ui/js/automation.js` | `tools/workbench/frontend/js/operate-workspace.js` | Complete |
| Parameters list/update (`GET/POST /v0/parameters`) with type/range validation | `tools/operator-ui/js/automation.js` | `tools/workbench/frontend/js/operate-workspace.js`, `js/operate/contracts.js` | Complete |
| Device inventory/detail (`/v0/devices`, `/v0/state/{p}/{d}`) | `tools/operator-ui/js/device-overview.js`, `js/device-detail.js` | `tools/workbench/frontend/js/operate-workspace.js` | Complete |
| Function invocation (`POST /v0/call`) with typed payload hardening | `tools/operator-ui/js/device-detail.js` | `tools/workbench/frontend/js/operate-workspace.js` | Complete |
| SSE event visibility: `state_update`, `quality_change`, `mode_change`, `parameter_change`, `bt_error`, `provider_health_change` | `tools/operator-ui/js/sse.js`, `js/automation.js` | `tools/workbench/frontend/js/operate/events.js`, `js/operate-workspace.js` | Complete |
| Bounded event trace buffer | `tools/operator-ui/js/automation.js` | `tools/workbench/frontend/js/operate/events.js`, `js/operate-workspace.js` | Complete |
| Telemetry access parity (embed + external fallback) | `tools/operator-ui/index.html`, `js/telemetry.js` | `tools/workbench/frontend/index.html`, `js/operate-workspace.js` | Complete |

Conclusion: No retirement-blocking capability remains exclusive to
`tools/operator-ui`.

## Verified Non-Blocking Differences

1. Layout and panel arrangement differs from legacy dashboard tabs.
2. Legacy theme toggle parity is not carried into Workbench Operate.
3. Legacy hash routes (for example `#devices/...`) are replaced by Workbench
   project/workspace routing.

These are intentional and non-blocking per Phase 07 scope.

## Evidence Anchors

1. Route/proxy behavior tests:
   - `python3 -m pytest tools/workbench/tests -q -s`
2. Workbench operate helper tests:
   - `node --test tools/workbench/tests/unit/*.test.mjs`
3. Legacy contract adapter baseline (kept until retirement):
   - `node --test tools/operator-ui/tests/contracts.test.mjs`
4. Local verification aggregate:
   - `bash tools/verify-local.sh`

## Residual Risk and Mitigation

1. Risk: Feature parity exists but operator muscle-memory still anchored to legacy layout.
2. Mitigation: Use migration runbook plus fallback window gates before deletion.

See:

1. [Operator UI to Workbench Migration Runbook](operator-ui-to-workbench-migration-runbook.md)
2. [Operator UI Retirement Go/No-Go Checklist](operator-ui-retirement-go-no-go.md)
