# Dependency Management Policy

Anolis uses **vcpkg** (manifest mode) for C++ dependencies and **pip** (requirements.txt) for Python tooling.

## Pinning Policy

To ensure reproducible builds across local development and CI:

1. **Single Source of Truth**: The `builtin-baseline` in `vcpkg.json` and the `baseline` in `vcpkg-configuration.json` MUST match.
2. **CI Alignment**: The environment variable `VCPKG_COMMIT` in `.github/workflows/ci.yml` must match the baseline in the JSON files.
3. **Locking**: We pin to a specific vcpkg git commit SHA (e.g., from a quarterly release tag).

## Update Cadence

- **Quarterly**: Review the latest vcpkg release tag.
- **Process**:
  1. Update `VCPKG_COMMIT` in `ci.yml`.
  2. Update `builtin-baseline` in `vcpkg.json`.
  3. Update `baseline` in `vcpkg-configuration.json`.
  4. Run a full clean build and test cycle.
  5. If dependencies break (ABI changes, API removal), fix code or peg specific package versions in `vcpkg.json` "overrides".

## CVE / Security Updates

- **Critical CVE**: If a critical vulnerability is found in a dependency (e.g., `cpp-httplib`), we will deviate from the quarterly cadence and bump the baseline immediately or override that specific package.

## Current Dependencies

| Package | Purpose |
| :--- | :--- |
| **protobuf** | Serialization format for ADPP protocol. |
| **yaml-cpp** | Parsing `anolis-runtime.yaml`. |
| **cpp-httplib** | Embedding the HTTP server. |
| **nlohmann-json** | JSON utility for API responses. |
| **behaviortree-cpp**| Automation engine. |
| **gtest** | Unit testing framework. |

## Troubleshooting

**"Works locally but fails in CI"**
- Check if your local vcpkg cache is stale. Run `git clean -fdx` and rebuild.
- Verify `vcpkg-configuration.json` matches `ci.yml` SHA.
