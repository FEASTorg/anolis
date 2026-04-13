# Schemas

Machine-readable contract artifacts for Anolis.

## Runtime Config Schema

File:

1. `runtime-config.schema.json`

Scope:

1. Runtime YAML consumed by `anolis-runtime --config` and `--check-config`.
2. Targets `anolis-runtime*.yaml` files under `config/` and composer runtime outputs under `systems/`.
3. Excludes provider-local YAML and telemetry-export YAML.

Compatibility notes for current wave:

1. Unknown keys are intentionally allowed by schema to match runtime loader behavior (warn-and-ignore).
2. Deprecated keys are still accepted:
   - `automation.behavior_tree_path`
   - flat `telemetry.influx_*`, `telemetry.batch_size`, `telemetry.flush_interval_ms`
3. Use runtime semantic validation (`anolis-runtime --check-config`) alongside schema validation.
