# Telemetry Timeseries Baseline

Status: Locked for prereq wave (`v1`).

## Purpose

Freeze the telemetry row contract used between runtime emission (`anolis_signal`) and telemetry exporter query/normalization logic.

## Canonical Artifacts

1. Machine schema: `schemas/telemetry/telemetry-timeseries.schema.v1.json`
2. Contract fixtures: `tests/contracts/telemetry-timeseries/examples/`
3. Fixture manifest: `tests/contracts/telemetry-timeseries/examples/manifest.yaml`
4. Contract validator: `tools/contracts/validate-telemetry-timeseries.py`
5. Runtime emission implementation: `core/telemetry/influx_sink.hpp`
6. Runtime emission unit coverage: `tests/unit/influx_sink_test.cpp`

## Scope

This baseline covers only measurement `anolis_signal`.

Out of scope in this wave:

1. `mode_change`
2. `parameter_change`

## Locked Contract Summary

## Measurement

1. `measurement` must be `anolis_signal`.

## Required tags

1. `runtime_name`
2. `provider_id`
3. `device_id`
4. `signal_id`

All required tags are non-empty strings.

## Required fields

1. `quality` is required on every row.
2. `quality` values are limited to:
   - `OK`
   - `STALE`
   - `UNAVAILABLE`
   - `FAULT`

## Typed value fields

Typed value fields are:

1. `value_double`
2. `value_int`
3. `value_uint`
4. `value_bool`
5. `value_string`

Cardinality rule:

1. At most one typed value field may be present.
2. Zero typed value fields are allowed for exceptional runtime cases (for example non-finite doubles where runtime omits the value field and still emits quality).

## Timestamp

1. Runtime writes timestamp as epoch milliseconds (`timestamp_ms` in logical contract representation).

## Compatibility Rule

For this baseline wave:

1. Additive changes to optional metadata are backward-compatible.
2. Removing required keys or changing key semantics is breaking.

## Validation Gate

Run:

```bash
python3 tools/contracts/validate-telemetry-timeseries.py
```

The gate must remain green in CI.

## Distribution Note

Short-term contract sync model:

1. `anolis` is schema source of truth.
2. `anolis-telemetry-export` consumes manual copied schema with checksum lock.

Long-term contract sync model:

1. `anolis` release publishes schema artifact + checksum.
2. `anolis-telemetry-export` consumes pinned release schema artifact.

## Change Rule

Update this baseline only in the same change that updates:

1. machine schema,
2. fixtures/validator expectations,
3. runtime/exporter contract behavior (if behavior changed).
