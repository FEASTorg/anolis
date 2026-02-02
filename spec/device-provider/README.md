# spec/device-provider

This directory defines the **Anolis Device Provider Protocol (ADPP)**, version **v0**.

ADPP defines the contract between:
- **Anolis (the orchestrator)**, and
- **Providers** (implementations that expose devices, signals, and callable functions).

Providers may represent hardware backends such as CRUMBS/I2C buses, GPIO/SPI devices, BLE devices, simulators, etc.

## Scope

ADPP v0 covers:
- Enumerating devices (`ListDevices`)
- Describing device capabilities (`DescribeDevice`)
- Reading telemetry signals (`ReadSignals`)
- Calling device functions (`Call`)
- Provider/device health (`GetHealth`)

ADPP v0 does **not** define:
- A particular transport (HTTP, gRPC, etc.)
- Authentication / authorization
- Time-series database schema
- Behavior Tree semantics

Those are handled by Anolis and/or deployment-specific infrastructure.

## Files

- `protocol.proto` — normative Protobuf schema for requests and responses.
- `semantics.md` — normative behavioral semantics (timeouts, caching, quality, errors, framing guidance).

## Versioning

- The schema is namespaced under `anolis.deviceprovider.v0`.
- Providers **must** report their supported `protocol_version`.
- Changes that break wire compatibility require a new major version directory (`v1`, `v2`, ...).

## Key design principles

- **Language-agnostic**: any language can implement a Provider by speaking ADPP.
- **Capability-first**: Providers describe what each device can do; Anolis binds behavior/config to capabilities.
- **Provider owns transport mechanics**: staged reads, bus scanning, caching are internal to the Provider.
- **Anolis owns orchestration semantics**: modes, manual leases, safety, arbitration.
