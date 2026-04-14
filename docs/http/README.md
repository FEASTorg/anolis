# Runtime HTTP Contract

Canonical HTTP contract artifacts for the runtime `/v0` surface.

## Source of Truth

1. OpenAPI spec: [`openapi.v0.yaml`](openapi.v0.yaml)
2. Implementation baseline: [`../contracts/runtime-http-baseline.md`](../contracts/runtime-http-baseline.md)
3. Runtime handlers/routes:
   - `core/http/server.cpp`
   - `core/http/handlers/*.cpp`

## Contract Policy

1. Config schema and HTTP contract are separate layers.
2. Shared concepts (mode enums, status code semantics, parameter value shapes) are parity-checked.
3. Shared cross-contract schema fragments are deferred to a later follow-up after HTTP contract stabilization.

## Validation

Run structural OpenAPI validation:

```bash
python3 tools/contracts/validate-runtime-http-openapi.py
```

The validator checks:

1. OpenAPI top-level structure.
2. Required endpoint/method coverage.
3. Response presence for required operations.
4. Internal `$ref` resolution.
5. SSE media type contract for `/v0/events`.

## Notes

1. `/v0/events` is intentionally documented with provisional schema depth in this initial contract wave.
2. Runtime behavior remains implementation-authoritative for semantics not yet captured as strict OpenAPI constraints.
