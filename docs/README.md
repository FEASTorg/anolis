# Anolis Documentation

Use this file as the canonical documentation index.

## Start Here

1. [getting-started.md](getting-started.md) - Build, run, and first validation.
2. [architecture.md](architecture.md) - Runtime architecture and subsystem boundaries.
3. [configuration.md](configuration.md) - How to author and validate runtime YAML.
4. [http-api.md](http-api.md) - Human guide for runtime `/v0` HTTP usage.

## Contracts and Schemas

1. [../schemas/README.md](../schemas/README.md) - Machine-readable contract artifacts.
2. [contracts/README.md](contracts/README.md) - Contract baselines and change policy.
3. [http/README.md](http/README.md) - Runtime HTTP contract validation workflow.

## Runtime Reference

1. [configuration-schema.md](configuration-schema.md) - Compact narrative map of schema sections.
2. [automation.md](automation.md) - Automation architecture, mode semantics, and operation.
3. [providers.md](providers.md) - Provider model and runtime interaction rules.
4. [safety.md](safety.md) - Safety boundaries and operational expectations.

## Contributor Reference

1. [local-verification.md](local-verification.md) - Fast local verification workflow.
2. [dependencies.md](dependencies.md) - Dependency and toolchain expectations.
3. [cpp-documentation-standard.md](cpp-documentation-standard.md) - C++ doc style rules.
4. [../CONTRIBUTING.md](../CONTRIBUTING.md) - Contributor workflow and repo policy.

## Workbench Transition

1. [workbench/operate-parity-matrix.md](workbench/operate-parity-matrix.md) - Phase 07 parity mapping for Workbench Operate and legacy Operator UI.
2. [workbench/operator-ui-to-workbench-migration-runbook.md](workbench/operator-ui-to-workbench-migration-runbook.md) - Task-level migration runbook.
3. [workbench/operator-ui-retirement-go-no-go.md](workbench/operator-ui-retirement-go-no-go.md) - Explicit retirement gate checklist and fallback terms.

Local C++ API docs: run `doxygen docs/Doxyfile` from repo root.
