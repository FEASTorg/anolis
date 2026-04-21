# Telemetry Timeseries Contract Fixtures

Fixtures for the `anolis_signal` telemetry row contract.

## Layout

1. `examples/manifest.yaml`
   - Lists every fixture file and expected outcome.
2. `examples/valid/*.json`
   - Must pass schema validation.
3. `examples/invalid/*.json`
   - Must fail schema validation.

## Validation

Run:

```bash
python3 tools/contracts/validate-telemetry-timeseries.py
```

## Coverage Goals

Fixtures must cover:

1. all typed value fields (`double`, `int`, `uint`, `bool`, `string`),
2. quality-only row (no typed value field),
3. invalid cardinality (multiple typed value fields),
4. invalid required data (missing quality, invalid measurement, bad tag data).
