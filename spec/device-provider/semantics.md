# ADPP v0 Semantics (normative)

This document defines normative semantics for the **Anolis Device Provider Protocol (ADPP)** v0.

## 1. Roles and responsibilities

### 1.1 Anolis (client)

Anolis is the ADPP client. It:

- orchestrates machine behavior (e.g., BehaviorTree execution)
- arbitrates manual vs autonomous control
- enforces safety policies and mode gating
- persists telemetry and events

### 1.2 Provider (server)

A Provider is an ADPP server that exposes devices and operations backed by some implementation (I2C/CRUMBS, GPIO, BLE, simulation, etc.). A Provider:

- discovers and reports devices (if discovery is supported)
- describes device capabilities (functions + signals)
- performs reads and calls
- manages transport/backend-specific mechanics (staged reads, caching, retries, bus scans)
- reports health

**Providers MUST NOT encode orchestration policy.**
Providers may validate inputs and reject invalid or unsafe device-level actions, but Anolis is the authority for:

- control modes (`AUTO`, `MANUAL`, etc.)
- manual leases
- machine-level safety interlocks spanning devices

## 2. Transport and framing

ADPP messages are defined in `protocol.proto` as `Request` and `Response` envelopes.

ADPP is transport-agnostic; however, when used over a byte-stream transport (recommended for local IPC):

- Each message MUST be framed using a length prefix followed by the serialized Protobuf message bytes.
- The length prefix SHOULD be an unsigned varint (common Protobuf framing) or a fixed 32-bit unsigned integer by mutual agreement.
- Implementations MUST handle message coalescing and fragmentation (a stream may deliver partial or multiple frames).

Rationale: Protobuf itself does not define framing; length-prefix framing is a standard approach for stream transports.

## 3. Session handshake

### 3.1 Hello

- The client SHOULD send `HelloRequest` first.
- The provider MUST respond with `HelloResponse`.
- If `protocol_version` is not supported, provider MUST respond with `Status.code = CODE_FAILED_PRECONDITION` or `CODE_UNIMPLEMENTED`.

## 4. Request/response correlation and ordering

- `request_id` MUST be unique per in-flight request within a session.
- Providers MUST echo the same `request_id` in the corresponding `Response`.
- Providers MAY process requests concurrently, therefore Responses MAY be out-of-order unless the transport enforces ordering and the provider chooses to serialize processing.

Clients MUST NOT assume in-order responses; they MUST correlate by `request_id`.

## 5. Inventory and identity

### 5.1 Device identity

- `Device.device_id` MUST be stable for the life of the Provider process and SHOULD be stable across restarts when feasible.
- `device_id` uniqueness scope is the Provider; Anolis should treat `{provider_name, device_id}` as globally unique.

### 5.2 Discovery

- A Provider MAY support discovery (e.g., scanning an I2C bus).
- `ListDevices` MUST return the current known devices.
- If the backend is non-discoverable, `ListDevices` MAY return a configured/static set or an empty list.

### 5.3 Filtering and backend mechanics

- Any bus scanning, filtering, or identification logic is Provider-internal.
- Example: a CRUMBS Provider may scan all I2C addresses and only report devices that respond to a CRUMBS identify/handshake.

## 6. Capabilities

### 6.1 DescribeDevice

- `DescribeDevice` MUST return the complete `CapabilitySet` for that device.
- Function and signal identifiers MUST be stable for a given device type/version.

### 6.2 Function IDs vs names

- `function_id` is the preferred stable identifier for calls.
- `function_name` is a convenience label for humans and UIs.
- If both are provided in `CallRequest`, Provider MUST prefer `function_id`.

### 6.3 Policy metadata

`FunctionPolicy` fields are **informational hints** for Anolis and UIs. In v0:

- Anolis is the authoritative enforcer of modes and leases.
- Providers MAY reject calls that violate `allowed_modes` (defensive), but Anolis must not rely on that.

## 7. Telemetry reads

### 7.1 ReadSignals behavior

- Providers MAY return cached values, live values, or a combination.
- Providers SHOULD update `SignalValue.timestamp` to reflect when the measurement was observed at the source.
- If a value is older than `SignalSpec.stale_after_ms` (when specified), provider SHOULD report `quality = QUALITY_STALE`.

### 7.2 Freshness hints

- If `ReadSignalsRequest.min_timestamp` is set:
  - Provider SHOULD attempt to return values with `timestamp >= min_timestamp` when feasible.
  - If not feasible, provider SHOULD still return the best available values and indicate staleness via `quality` and/or `Status.details`.

### 7.3 Missing signals

If a requested signal is unknown:

- Provider SHOULD respond with `Status.code = CODE_NOT_FOUND`
- or return partial results with `Status.code = CODE_OK` and omit the missing signal(s),
  but MUST choose a single consistent approach and document it in provider metadata.

(Recommendation for v0: fail the request with `CODE_NOT_FOUND` to simplify client logic.)

## 8. Function calls

### 8.1 Synchronous vs asynchronous

- Providers MAY implement calls synchronously (apply and return results)
- Providers MAY accept calls asynchronously and return:
  - `Status.code = CODE_OK`
  - `operation_id` set to a provider-defined identifier
  - empty or partial results

ADPP v0 does not define an operation polling API; if async is used, the provider must document how the resulting state is observed (usually via signals).

(Recommendation for v0: keep calls synchronous unless unavoidable.)

### 8.2 Idempotency

- `CallRequest.idempotency_key` is an optional hint for retry handling.
- Providers MAY ignore it in v0.
- If honored, providers should treat repeated calls with the same `(device_id, function_id, idempotency_key)` as safe replays.

### 8.3 Input validation

Providers MUST validate:

- device existence
- function existence
- argument presence for required args
- argument type correctness
- numeric bounds when provided in `ArgSpec`

Invalid inputs MUST return `CODE_INVALID_ARGUMENT` (or `CODE_NOT_FOUND` for unknown identifiers).

## 9. Health reporting

### 9.1 Provider health

- `GetHealth` MUST return `ProviderHealth`.
- `ProviderHealth.state` should reflect the provider’s ability to service requests.

### 9.2 Device health

- `DeviceHealth` should reflect reachability and freshness.
- `last_seen` SHOULD be updated whenever the provider successfully communicates with the device backend.

## 10. Error handling

- Every `Response` MUST include `Status`.
- `Status.code = CODE_OK` indicates success.
- Errors SHOULD include a human-readable `Status.message`.
- `Status.details` MAY include structured key/value debugging info.

## 11. Backward compatibility

- Providers and clients MUST treat unknown fields as ignorable (standard Protobuf behavior).
- New fields may be added in v0 without breaking older clients.
- Changing field numbers or semantics in an incompatible way requires a new major version (`v1`).

## 12. Security considerations (non-normative for v0)

ADPP v0 does not define authentication. For local IPC, rely on OS-level permissions (e.g., Unix socket file permissions). For network transports, wrap ADPP in a secured channel appropriate to the deployment.
