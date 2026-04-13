# Runtime Config Baseline

Status: locked baseline for contract implementation.

Purpose: capture current runtime-config behavior before adding JSON Schema and CI gates.

## Source of Truth (Current Implementation)

This baseline is derived from:

1. `core/runtime/config.hpp` (config model + defaults)
2. `core/runtime/config.cpp` (loader compatibility + validation rules)
3. `core/src/main.cpp` (`--check-config` behavior)
4. `tests/unit/config_test.cpp` (behavior coverage)

## Runtime Config File Scope

This baseline applies only to runtime YAML consumed by `anolis-runtime --config` and `--check-config`.

Target patterns:

1. `config/anolis-runtime*.yaml`
2. `config/**/anolis-runtime*.yaml`
3. `systems/**/anolis-runtime.yaml`

Out of scope:

1. `config/**/provider-*.yaml`
2. `config/**/telemetry-export*.yaml`

## Top-Level Sections

Supported top-level sections:

1. `runtime`
2. `http`
3. `providers`
4. `polling`
5. `telemetry`
6. `logging`
7. `automation`

Unknown top-level keys are warned and ignored.

## Compatibility Behavior (Locked)

Current compatibility behavior that must remain unchanged during initial contract rollout:

1. Unknown keys:
   - warned and ignored (top-level and nested map sections)
2. Deprecated but accepted:
   - `automation.behavior_tree_path` alias for `automation.behavior_tree`
   - flat telemetry keys under `telemetry.*`:
     - `influx_url`
     - `influx_org`
     - `influx_bucket`
     - `influx_token`
     - `batch_size`
     - `flush_interval_ms`
3. Rejected legacy key:
   - `runtime.mode` is hard-failed

## Defaults Snapshot

Defaults from `config.hpp`:

1. `runtime.shutdown_timeout_ms = 2000`
2. `runtime.startup_timeout_ms = 30000`
3. `http.enabled = true`
4. `http.bind = "127.0.0.1"`
5. `http.port = 8080`
6. `http.cors_allowed_origins = ["*"]`
7. `http.cors_allow_credentials = false`
8. `http.thread_pool_size = 40`
9. `polling.interval_ms = 500`
10. `logging.level = "info"`
11. `telemetry.enabled = false`
12. `telemetry.influx_url = "http://localhost:8086"`
13. `telemetry.influx_org = "anolis"`
14. `telemetry.influx_bucket = "anolis"`
15. `telemetry.batch_size = 100`
16. `telemetry.flush_interval_ms = 1000`
17. `telemetry.queue_size = 10000`
18. `telemetry.max_retry_buffer_size = 1000`
19. `automation.enabled = false`
20. `automation.tick_rate_hz = 10`
21. `automation.manual_gating_policy = BLOCK`

## Validation Rules Snapshot

Current loader-enforced rules:

1. Runtime:
   - `500 <= shutdown_timeout_ms <= 30000`
   - `5000 <= startup_timeout_ms <= 300000`
2. HTTP (when enabled):
   - `1 <= port <= 65535`
   - `thread_pool_size >= 1`
   - `cors_allowed_origins` non-empty
   - wildcard origin `*` forbidden when `cors_allow_credentials=true`
3. Providers:
   - non-empty `providers`
   - unique provider `id`
   - `command` required
   - `timeout_ms >= 100`
   - `hello_timeout_ms >= 100`
   - `ready_timeout_ms >= 1000`
4. Restart policy (when enabled):
   - `max_attempts >= 1`
   - `backoff_ms` non-empty
   - `len(backoff_ms) == max_attempts`
   - each backoff value `>= 0`
   - `timeout_ms >= 1000`
   - `success_reset_ms >= 0`
5. Polling:
   - `interval_ms >= 100`
6. Logging:
   - level in `debug|info|warn|error`
7. Automation (when enabled):
   - `behavior_tree` required
   - `1 <= tick_rate_hz <= 1000`
   - typed parameter default consistency
   - numeric `min/max` only for numeric parameter types
   - `allowed_values` only for string parameters
   - mode transition hooks:
     - `from`/`to` in `MANUAL|AUTO|IDLE|FAULT|*` (or empty)
     - non-empty `calls`
     - each call requires `device_handle`
     - each call requires one of `function_id` or `function_name`

## Automation Hook Arg Scalar Parsing

`automation.mode_transition_hooks.*.calls[].args` currently parse using loader scalar heuristics:

1. `true|false` -> bool
2. integer literal -> int64
3. float literal -> double
4. anything else -> string

Quoted numeric-like strings can be coerced by YAML scalar parsing before this logic, so fixtures must explicitly cover this edge case.

## Known Baseline Quirks (Intentional for Now)

These are baseline behaviors and not changed in the first contract wave:

1. Duplicate automation parameter names are accepted by config load.
2. Later parameter redefinitions are ignored by `ParameterManager::define` with a warning.
3. Unknown keys are warnings, not hard failures.

## Existing Coverage Reference

Representative unit coverage in `tests/unit/config_test.cpp`:

1. unknown key tolerance
2. `runtime.mode` rejection
3. telemetry nested + defaults
4. automation alias (`behavior_tree_path`) acceptance
5. mode-transition hook parsing and validation
6. CORS constraints
7. restart policy invariants
8. idempotent parsing and default reset semantics

## Next Contract Step

Implement schema and validator script against this baseline without changing runtime behavior.
