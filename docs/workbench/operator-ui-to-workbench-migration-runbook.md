# Operator UI to Workbench Migration Runbook

Status: Active for Phase 07 closeout and Phase 08 handoff window.  
Last reviewed: 2026-04-16.

Purpose: Define task-level migration from legacy `tools/operator-ui` to
Workbench Operate without changing runtime contracts.

## Scope and Guardrails

1. Workbench Operate is the primary operator surface.
2. Legacy `tools/operator-ui` remains fallback during retirement sign-off.
3. Runtime contract authority stays on `/v0/*` and `/v0/events`.

## Preconditions

1. Runtime is launchable from Workbench Commission for the target project.
2. Runtime HTTP endpoints are reachable through Workbench pass-through (`/v0/*`).
3. Workbench Operate parity tests and route/proxy tests are green.

## Task Mapping (Legacy -> Workbench)

### 1. Confirm Runtime and Stream Health

1. Legacy: Header badges in `operator-ui` dashboard.
2. Workbench: Operate status strip (`Runtime API`, `Event Stream`).

Expected parity outcome:

1. Runtime unavailable is explicit.
2. Stream transitions (`connected/reconnecting/disconnected/stale`) are visible.

### 2. Change Runtime Mode

1. Legacy: Automation panel mode selector + Set.
2. Workbench: Operate `Runtime Mode` card selector + Set.

Expected parity outcome:

1. `POST /v0/mode` success updates current mode badge.
2. Errors are shown inline without workspace crash.

### 3. Review Automation Runtime Health

1. Legacy: BT status/ticks/errors section.
2. Workbench: `Automation Status` card.

Expected parity outcome:

1. Status, tick counters, error count, current tree, and last error are visible.

### 4. Inspect and Update Runtime Parameters

1. Legacy: Parameters panel with typed update controls.
2. Workbench: `Runtime Parameters` panel.

Expected parity outcome:

1. Types supported: `double`, `int64`, `bool`, `string`.
2. Range violations fail with clear inline feedback.
3. Allowed-values violations fail with clear inline feedback.

### 5. Inspect Devices and State

1. Legacy: Devices tab/detail panel.
2. Workbench: `Devices` + `Device Detail` cards.

Expected parity outcome:

1. Device list, signal values, quality, and timestamps render correctly.
2. Selected device updates remain stable under live SSE updates.

### 6. Execute Device Functions

1. Legacy: Function forms in device detail.
2. Workbench: Function cards in `Device Detail`.

Expected parity outcome:

1. Typed arguments are encoded to runtime contract shape.
2. Invalid arguments are rejected client-side where applicable.
3. Call success/failure feedback is visible.

### 7. Track Runtime Events

1. Legacy: Event trace list in automation panel.
2. Workbench: `Event Trace` panel.

Expected parity outcome:

1. Required event families are present.
2. Buffer is bounded and newest events are shown first.

### 8. Open Telemetry

1. Legacy: Telemetry tab with iframe + external link.
2. Workbench: `Telemetry` card with iframe + explicit external fallback.

Expected parity outcome:

1. Embedded Grafana loads when allowed.
2. External fallback remains usable if embedding is blocked.

## Fallback Procedure During Sign-Off Window

1. If Workbench Operate fails a retirement-blocking scenario, continue using
   `tools/operator-ui` for that scenario.
2. Open an issue with exact failing endpoint/event path and reproduction steps.
3. Do not retire `tools/operator-ui` until checklist gate is fully green.

## Exit Evidence for Migration Completion

1. Execute runbook tasks above on at least one representative commissioned
   project.
2. Attach command evidence for:
   - `python3 -m pytest tools/workbench/tests -q -s`
   - `node --test tools/workbench/tests/unit/*.test.mjs`
   - `node --test tools/operator-ui/tests/contracts.test.mjs`
3. Confirm go/no-go checklist is fully passed.

See [Operator UI Retirement Go/No-Go Checklist](operator-ui-retirement-go-no-go.md).
