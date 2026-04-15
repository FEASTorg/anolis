# Tooling Interface Schemas

Machine-readable tooling contracts that are not part of runtime `/v0` API.

## Composer Control API OpenAPI

File:

1. `composer-control.openapi.v1.yaml`

Scope:

1. System Composer control endpoints for commissioning orchestration:
   - status
   - preflight
   - launch
   - stop
   - restart
   - logs (SSE)
2. Complements runtime `/v0` API contract (`schemas/http/runtime-http.openapi.v0.yaml`).
3. Validated by `tools/contracts/validate-composer-control-openapi.py`.
