# Operator UI Retirement Go/No-Go Checklist

Status: Required gate before recommending `tools/operator-ui` retirement.  
Last reviewed: 2026-04-16.

Purpose: Prevent premature retirement by enforcing explicit sign-off criteria,
verification evidence, and fallback window terms.

## Decision Outcome

1. `GO`: retirement recommendation is approved (deprecation path can continue).
2. `NO-GO`: keep `tools/operator-ui` as active fallback and resolve gaps first.

## Mandatory Technical Gates

All items below must be `PASS` for `GO`.

| Gate | Pass/Fail | Evidence |
| --- | --- | --- |
| Workbench route/proxy tests pass including stopped/unreachable runtime behavior | Pass (2026-04-16) | `python3 -m pytest tools/workbench/tests -q -s` |
| Workbench operate unit tests pass (contracts/events/guards) | Pass (2026-04-16) | `node --test tools/workbench/tests/unit/*.test.mjs` |
| Legacy operator-ui fixture contract tests still pass | Pass (2026-04-16) | `node --test tools/operator-ui/tests/contracts.test.mjs` |
| Aggregate local verification is green except documented environment skips | Pass (2026-04-16) | `bash tools/verify-local.sh` |
| No retirement-blocking capability remains exclusive to operator-ui | Pass (2026-04-16) | `docs/workbench/operate-parity-matrix.md` |

## Mandatory Operational Gates

All items below must be `PASS` for `GO`.

| Gate | Pass/Fail | Evidence |
| --- | --- | --- |
| Migration runbook executed successfully on representative project(s) | Pending | `docs/workbench/operator-ui-to-workbench-migration-runbook.md` |
| Runtime unavailable and runtime-unreachable UX behavior is deterministic in Workbench Operate | Pending | Recorded run + screenshots/logs |
| Parameter validation errors are actionable (range/type/allowed-values) | Pending | Recorded run + screenshots/logs |
| SSE reconnect and stale transitions are visible in stream badge + event trace | Pending | Recorded run + screenshots/logs |
| Telemetry embed path and external fallback path both verified | Pending | Recorded run + screenshots/logs |

## Fallback Window Terms

`tools/operator-ui` must remain available until all terms are met:

1. All mandatory technical and operational gates are `PASS`.
2. Phase 08 kickoff review acknowledges that Phase 07 parity risk is closed.
3. At least one full commissioning cycle has been completed using Workbench
   Operate as primary surface.

If any gate regresses during Phase 08, status immediately returns to `NO-GO` and
`tools/operator-ui` remains fallback.

## Sign-Off Block

| Role | Name | Date | Decision |
| --- | --- | --- | --- |
| Runtime owner |  |  |  |
| Workbench owner |  |  |  |
| Commissioning/operator representative |  |  |  |
| Project lead |  |  |  |

Decision notes:

1. 
2. 
3. 
