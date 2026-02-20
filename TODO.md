# Anolis Runtime - TODO

## CI / Quality

- [ ] Add a Linux coverage CI lane (`-DENABLE_COVERAGE=ON`) with artifact + summary reporting.
- [ ] Add mypy CI lane for Python scripts/tests (ruff remains lint/format gate).
- [ ] Roll out warnings-as-errors for first-party C++ (`-Werror`/`/WX`) in staged mode.
- [ ] Add Aarch64 CI build/test lane (Raspberry Pi deployment path).
- [ ] Add fuzzing targets for ADPP/protocol and runtime config parsing surfaces.
- [x] Migrate to config-only vcpkg baseline and update in the same change:
  - `scripts/setup.ps1`
  - `scripts/setup.sh`
  - `docs/dependencies.md`

## Security / Production Hardening

- [ ] Implement HTTP/API authentication and authorization before non-localhost exposure.
- [ ] Add dependency/CVE automation (Dependabot + security advisory checks).
- [ ] Define and enforce telemetry redaction/classification + secret-safe logging policy.

## Performance / Reliability

- [ ] Run valgrind leak analysis on Linux for runtime lifecycle paths.
  - Command: `valgrind --leak-check=full --show-leak-kinds=all ./build/core/Release/anolis-runtime --config test.yaml`
  - Focus: event emitter queues, provider supervision, HTTP server threads
- [ ] Add benchmark baselines and regression tracking for core runtime paths.
- [ ] Run long-duration soak/stress tests (>24h), including rapid provider restart cycles.
- [ ] Conditional: move to async provider polling only if provider-count/latency metrics demand it.
- [ ] Conditional: run lock-contention profiling and tune identified hotspots.

## Operator UI (Demand-Driven)

- [ ] Event filtering/search in event trace.
- [ ] Session export + replay support.
- [ ] Device state snapshots for validation/reporting.
- [ ] Runtime-configurable polling interval.

## Protocol / Hardware Evolution

- [ ] Re-evaluate ADPP Configure extension only if decentralized provider config proves insufficient.
- [ ] Expand hardware mock/fault-injection strategy for broader real-hardware validation.

## Ecosystem / Provider SDKs (Late Future)

- [ ] Design and ship provider SDKs (C/C++/Rust) only after:
  - at least 3 real hardware providers are implemented and validated
  - runtime/app interfaces and ADPP/provider integration seams have stabilized
  - SDK versioning/compatibility policy is defined (including deprecation strategy)
