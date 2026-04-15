# Composer Control Baseline

Status: Locked.

## Purpose

Freeze System Composer control API behavior used by commissioning flows so UI and tooling integrations remain stable.

## Canonical Artifacts

1. Backend implementation: `tools/system-composer/backend/server.py`
2. OpenAPI contract: `schemas/tools/composer-control.openapi.v1.yaml`
3. OpenAPI validator: `tools/contracts/validate-composer-control-openapi.py`
4. Contract tests: `tools/system-composer/tests/unit/test_control_contract.py`
5. Tooling docs:
   - `tools/system-composer/README.md`
   - `docs/http/README.md` (contract workflow context)

## Locked Behavior Summary

### Endpoint Inventory

1. `GET /api/status`
2. `POST /api/projects/{name}/preflight`
3. `POST /api/projects/{name}/launch`
4. `POST /api/projects/{name}/stop`
5. `POST /api/projects/{name}/restart`
6. `GET /api/projects/{name}/logs` (SSE)

### Response and Error Shape

1. Control endpoints return stable JSON success payloads.
2. Error payloads expose stable top-level `error` text.
3. Validation-failure payloads include structured `errors[]` entries.
4. Expected status semantics:
   - `400` invalid project names
   - `404` unknown project
   - `409` launch conflict
   - `500` backend failure

### Runtime/Path Decoupling

1. Composer path resolution is repo-anchored (`backend/paths.py`), not caller-CWD dependent.
2. Control-plane environment knobs remain:
   - `ANOLIS_COMPOSER_HOST`
   - `ANOLIS_COMPOSER_PORT`
   - `ANOLIS_OPERATOR_UI_BASE`
   - `ANOLIS_COMPOSER_OPEN_BROWSER`

## Validation Gates

1. `python3 tools/contracts/validate-composer-control-openapi.py`
2. `python3 -m pytest tools/system-composer/tests -q`

## Drift Notes and Change Rule

1. Additive response fields are allowed.
2. Existing fields must keep semantics.
3. Status-code or payload-shape changes require synchronized updates to implementation, OpenAPI, tests, and this baseline.
