# Anolis Documentation

Canonical docs for Anolis runtime, contracts, and operations.

## Start Here

1. [getting-started.md](getting-started.md) - Build and run.
2. [architecture.md](architecture.md) - Core system structure.
3. [configuration.md](configuration.md) - Runtime config usage.
4. [http-api.md](http-api.md) - Runtime `/v0` API reference.

## Contracts and Schemas

1. [../schemas/README.md](../schemas/README.md) - Machine-validated contract artifacts.
2. [contracts/README.md](contracts/README.md) - Contract baseline index and policy.
3. [http/README.md](http/README.md) - Runtime HTTP contract workflow and validation.

## Runtime Reference

1. [configuration-schema.md](configuration-schema.md) - Human-readable runtime schema guide.
2. [providers.md](providers.md) - Provider model and protocol usage.
3. [automation.md](automation.md) - Automation/BT behavior model.
4. [safety.md](safety.md) - Safety behavior and operational constraints.
5. [local-verification.md](local-verification.md) - Focused local verification workflow.

## Contributor Reference

1. [concept.md](concept.md) - Problem framing and design intent.
2. [dependencies.md](dependencies.md) - Build/runtime dependency notes.
3. [cpp-documentation-standard.md](cpp-documentation-standard.md) - C++ documentation conventions.
4. [../CONTRIBUTING.md](../CONTRIBUTING.md) - Full contributor workflow and pitfalls.

Local C++ API docs: run `doxygen docs/Doxyfile` from the repo root.
Generated output goes to `build/docs/doxygen/html/` and remains untracked.
