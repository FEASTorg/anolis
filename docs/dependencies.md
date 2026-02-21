# Dependency, Build, and CI Governance

Anolis uses:
- **vcpkg** (manifest mode) for C++ dependencies
- **pip** (`requirements*.txt`) for Python tooling/tests

This document is the Phase 31 governance baseline for dependency pinning, CI lanes, presets, and cross-repo compatibility.

## vcpkg Policy

1. **Single baseline source**: `vcpkg-configuration.json` is canonical.
2. **No `builtin-baseline`** in manifests.
3. **Lockfile pinning deferred** for now.
4. **Determinism source**: pinned baseline + reviewed `vcpkg.json` changes.

## Cross-Repo Pinning and Compatibility

Anolis tracks provider/runtime compatibility with two control files:
- `.ci/dependency-pins.yml`: refs consumed by compatibility lanes
- `.ci/compatibility-matrix.yml`: tested runtime/provider/protocol/fluxgraph combinations

Rules:
1. Pin updates and matrix updates must be in the same reviewed PR.
2. Compatibility lane must consume pinned refs, never floating `main`.
3. Pin changes require rationale and date.

## Version Numbering Policy (Cross-Repo)

- `anolis`, `anolis-provider-sim`, `fluxgraph`, and `anolis-protocol` version independently using **SemVer** (`MAJOR.MINOR.PATCH`).
- Public contract/build-surface changes require version-bump decision + changelog note.
- Compatibility matrix records tested cross-repo version/ref combinations.

## CI Lane Tiers

- **Required PR lanes**: merge-blocking.
- **Advisory lanes**: visible, non-blocking.
- **Nightly/periodic lanes**: heavy coverage/sanitizer/stress lanes.

Promotion rule:
- Advisory lane can be promoted to required only after **10 consecutive green default-branch runs** and an explicit promotion PR.

## Dual-Run Policy

When replacing legacy build/test/CI paths:
1. Run legacy and new paths in parallel.
2. Minimum gate: **5 consecutive green runs**.
3. Preferred gate: **10 runs**.
4. Remove legacy path only after gate is met and approved.

## Preset Baseline and Exception Policy

Shared preset naming baseline:
- `dev-debug`, `dev-release`, `ci-linux-release`, `ci-windows-release`
- Specialized where supported: `ci-asan`, `ci-ubsan`, `ci-tsan`, `ci-coverage`

Rules:
1. CI jobs should call named presets directly.
2. CI-only deviations must be explicit and documented.
3. Repo-specific extension presets are allowed if documented (for example feature-specific lanes).

## Current C++ Dependencies

| Package              | Purpose                                 |
| :------------------- | :-------------------------------------- |
| **protobuf**         | Serialization format for ADPP protocol. |
| **yaml-cpp**         | Parsing runtime/provider YAML config.   |
| **cpp-httplib**      | Embedded HTTP server.                   |
| **nlohmann-json**    | JSON handling.                          |
| **behaviortree-cpp** | Automation engine.                      |
| **gtest**            | Unit testing framework.                 |

## Update Workflow (Summary)

1. Update dependency refs/policies in one PR.
2. Validate required lanes and compatibility lane.
3. Update compatibility matrix + changelog note.
4. Merge only after lane tier and dual-run rules are satisfied.
