# Composer Control API Baseline

Status: Baseline snapshot for workbench interface lock (Phase 05).

Purpose:

1. Freeze the System Composer control API behavior used by commissioning flows.
2. Make response/error semantics explicit before larger workbench integration.
3. Provide a single reference for tests, docs, and compatibility decisions.

## Implemented Endpoint Inventory

Source of truth: `tools/system-composer/backend/server.py`.

1. `GET /api/status`
2. `POST /api/projects/{name}/preflight`
3. `POST /api/projects/{name}/launch`
4. `POST /api/projects/{name}/stop`
5. `POST /api/projects/{name}/restart`
6. `GET /api/projects/{name}/logs` (SSE)

These endpoints are part of the commissioning control surface.

## Success Response Baseline

## `GET /api/status` (200)

Response fields:

1. `version` (number, currently `1`)
2. `active_project` (string or `null`)
3. `running` (bool)
4. `pid` (number or `null`)
5. `composer` object:
   - `host` (string)
   - `port` (number)
   - `operator_ui_base` (string)

## `POST /api/projects/{name}/preflight` (200)

Response fields:

1. `ok` (bool)
2. `checks` (array of check objects)

## `POST /api/projects/{name}/launch` (200)

1. `{"ok": true}`

## `POST /api/projects/{name}/stop` (200)

1. `{"ok": true}`

## `POST /api/projects/{name}/restart` (200)

1. `{"ok": true}`

## `GET /api/projects/{name}/logs` (200, `text/event-stream`)

SSE log stream with keepalive comments and line-oriented `data:` frames.

## Error Response Baseline

Common error payload shape:

1. JSON object with an `error` string.

Structured validation error shape (`PUT /api/projects/{name}` save path):

1. `error` (string, currently `"Project validation failed"`)
2. `code` (string, currently `"validation_failed"`)
3. `errors` (array of objects with `source`, `code`, `path`, `message`)

Control endpoint status semantics:

1. `400` for invalid project names.
2. `404` for unknown project names on control endpoints.
3. `409` for launch conflict when another system is already running.
4. `500` for unexpected backend failures.

## Runtime/Path Decoupling Baseline

Composer path resolution is repo-anchored via `backend/paths.py` and is not tied
to the caller's current working directory.

Runtime behavior controlled by environment:

1. `ANOLIS_COMPOSER_HOST`
2. `ANOLIS_COMPOSER_PORT`
3. `ANOLIS_OPERATOR_UI_BASE`
4. `ANOLIS_COMPOSER_OPEN_BROWSER` (`1/0`, default enabled)

## Compatibility Policy

For this control API baseline:

1. Additive response fields are allowed.
2. Existing fields must keep meaning.
3. Status-code changes or payload-shape changes require baseline/test updates in
   the same change.

## Contract Ownership and Gates

Authoritative artifacts:

1. Baseline doc: `docs/contracts/composer-control-baseline.md`
2. Implementation: `tools/system-composer/backend/server.py`
3. Contract tests: `tools/system-composer/tests/unit/test_control_contract.py`

Local/CI coverage:

1. `python -m pytest tools/system-composer/tests -q`
