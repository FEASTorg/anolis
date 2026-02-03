# ADPP v0 Semantics (normative)

This document defines the **normative semantics** of the
**Anolis Device Provider Protocol (ADPP)** v0.

---

## 1. Roles and responsibilities

### 1.1 Anolis (client)

Anolis is the ADPP client. It is responsible for:

- Orchestrating machine behavior (e.g., Behavior Trees)
- Arbitrating manual vs autonomous control
- Enforcing safety policies and operating modes
- Persisting telemetry, state, and events
- Exposing external APIs (HTTP, dashboards, etc.)

### 1.2 Provider (server)

A Provider is an ADPP server that exposes devices backed by some implementation
(I2C/CRUMBS, GPIO, BLE, simulation, etc.).

A Provider is responsible for:

- Discovering and reporting devices (when applicable)
- Describing device capabilities (functions and signals)
- Executing reads and calls
- Managing backend-specific mechanics:
  - staged reads
  - caching
  - retries and backoff
  - bus scanning and filtering
- Reporting provider and device health

**Providers MUST NOT encode orchestration policy.**

Providers may defensively validate inputs and reject invalid or unsafe
device-level actions, but Anolis is the sole authority for:

- Control modes (`AUTO`, `MANUAL`, `IDLE`, `SAFE`, etc.)
- Manual control leases
- Cross-device safety interlocks
- Behavior execution semantics

---

## 2. Transport and framing

ADPP messages are defined in `protocol.proto` using `Request` and `Response`
envelopes.

ADPP itself is transport-agnostic. When used over a byte-stream transport
(recommended for local IPC):

- Each message MUST be framed using a length prefix followed by serialized
  Protobuf bytes.
- The length prefix SHOULD be:
  - an unsigned varint, or
  - a fixed-width 32-bit unsigned integer,
    by mutual agreement.
- Implementations MUST handle fragmentation and coalescing
  (a stream may deliver partial or multiple frames).

Rationale: Protobuf does not define framing; length-prefixed framing is a
well-established pattern.

---

## 3. Session handshake

### 3.1 Hello

- The client SHOULD send `HelloRequest` as the first message.
- The provider MUST respond with `HelloResponse`.
- If the requested `protocol_version` is not supported, the provider MUST
  respond with:
  - `CODE_FAILED_PRECONDITION`, or
  - `CODE_UNIMPLEMENTED`.

---

## 4. Request / response correlation

- `request_id` MUST be unique per in-flight request within a session.
- Providers MUST echo the same `request_id` in the corresponding `Response`.
- Providers MAY process requests concurrently.
- Responses MAY arrive out of order.

Clients MUST correlate responses using `request_id` and MUST NOT assume
ordering.

---

## 5. Inventory and identity

### 5.1 Device identity

- `Device.device_id` MUST be stable for the lifetime of the Provider process.
- Providers SHOULD preserve `device_id` stability across restarts when feasible.
- Uniqueness is scoped to the Provider; Anolis SHOULD treat
  `{provider_name, device_id}` as globally unique.

### 5.2 Discovery

- Providers MAY support discovery (e.g., scanning an I2C bus).
- `ListDevices` MUST return the currently known devices.
- Non-discoverable backends MAY return a static set or an empty list.

### 5.3 Backend filtering

All bus scanning, filtering, probing, or identification logic is
**provider-internal**.

Example: a CRUMBS Provider may scan all I2C addresses and only report devices
that respond to a CRUMBS identify handshake.

---

## 6. Capabilities

### 6.1 DescribeDevice

- `DescribeDevice` MUST return the complete `CapabilitySet` for the device.
- Function IDs and signal IDs MUST be stable for a given device type/version.

### 6.2 Function identifiers

- `function_id` is the preferred stable identifier.
- `function_name` is a human-readable convenience.
- If both are provided in a `CallRequest`, the provider MUST prefer
  `function_id`.

### 6.3 Policy metadata

`FunctionPolicy` fields are **informational hints** for Anolis and UIs.

In v0:

- Anolis is the authoritative enforcer of modes and leases.
- Providers MAY defensively reject calls that violate policy metadata, but
  Anolis MUST NOT rely on provider enforcement.

---

## 7. Telemetry reads

### 7.1 ReadSignals behavior

- Providers MAY return cached values, live values, or a combination.
- Providers SHOULD set `SignalValue.timestamp` to the time the measurement was
  observed at the source.
- If a value exceeds `SignalSpec.stale_after_ms`, providers SHOULD report
  `QUALITY_STALE`.

### 7.2 Default signals

Each provider MUST define, per device type, a **stable subset of signals**
designated as **default signals**.

- Default signals are returned when `ReadSignalsRequest.signal_ids` is empty.
- Default signals SHOULD represent low-cost, routinely useful telemetry
  suitable for dashboards and polling loops.
- Expensive or rarely-used signals SHOULD NOT be default.

This requirement ensures predictable behavior across providers.

### 7.3 Freshness hints

If `ReadSignalsRequest.min_timestamp` is provided:

- Providers SHOULD attempt to satisfy the freshness requirement.
- If not feasible, providers SHOULD return the best available values and
  indicate staleness via `quality` and/or `Status.details`.

### 7.4 Missing signals

If a requested signal is unknown, providers MUST choose one consistent
behavior:

- Fail the request with `CODE_NOT_FOUND`, **or**
- Return partial results and omit unknown signals.

(Recommended for v0: fail with `CODE_NOT_FOUND` to simplify client logic.)

---

## 8. Function calls

### 8.1 Execution model

- Providers MAY implement calls synchronously.
- Providers MAY accept calls asynchronously and return an `operation_id`.

ADPP v0 does not define an operation lifecycle API. If async calls are used,
providers MUST document how results are observed (typically via signals).

(Recommended for v0: synchronous execution.)

### 8.2 Idempotency

- `idempotency_key` is an optional retry hint.
- Providers MAY ignore it in v0.
- If honored, providers SHOULD treat repeated calls with the same key as safe
  replays.

### 8.3 Validation

Providers MUST validate:

- Device existence
- Function existence
- Required arguments
- Argument types
- Numeric bounds (when specified)

Invalid inputs MUST result in:

- `CODE_INVALID_ARGUMENT`, or
- `CODE_NOT_FOUND` for unknown identifiers.

---

## 9. Health reporting

### 9.1 Provider health

- `GetHealth` MUST return `ProviderHealth`.
- `ProviderHealth.state` reflects the provider’s overall ability to service
  requests.

### 9.2 Device health

- `DeviceHealth` reflects reachability and freshness.
- `last_seen` SHOULD be updated whenever the provider successfully communicates
  with the device backend.

---

## 10. Error handling

- Every `Response` MUST include a `Status`.
- `CODE_OK` indicates success.
- Errors SHOULD include a human-readable message.
- `Status.details` MAY include structured diagnostic metadata.

---

## 11. Backward compatibility

- Unknown fields MUST be ignored (standard Protobuf behavior).
- New optional fields may be added within v0.
- Incompatible semantic or wire changes require a new major version (`v1`).

---

## 12. Security considerations (non-normative)

ADPP v0 does not define authentication.

For local IPC, rely on OS-level permissions (e.g., Unix socket ownership).
For network transports, ADPP SHOULD be wrapped in an authenticated and
encrypted channel appropriate to the deployment.
