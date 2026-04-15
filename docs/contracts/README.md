# Contract Baselines

Baseline snapshots for contract workstreams.

Files:

1. [runtime-config-baseline.md](runtime-config-baseline.md) - runtime config contract baseline and drift notes.
2. [runtime-http-baseline.md](runtime-http-baseline.md) - runtime HTTP contract baseline and drift notes.
3. [machine-profile-baseline.md](machine-profile-baseline.md) - machine profile contract baseline and drift notes.
4. [composer-control-baseline.md](composer-control-baseline.md) - system-composer control API baseline and drift notes.

Related machine-readable artifacts live under `schemas/`:

1. Runtime HTTP OpenAPI: `schemas/http/runtime-http.openapi.v0.yaml`
2. Composer control OpenAPI: `schemas/tools/composer-control.openapi.v1.yaml`

These files are implementation anchors for contract execution slices and should
be updated only when the corresponding contract baseline intentionally changes.
