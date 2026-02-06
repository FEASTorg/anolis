# Anolis Automation Layer

Automation system for orchestrating device control using behavior trees.

## Overview

The automation layer adds configurable machine behavior on top of Anolis's stable IO primitives. It provides:

- **Behavior Trees** — Composable, reactive control logic that orchestrates device calls
- **Runtime Modes** — State machine governing when automation runs and how manual control interacts
- **Parameter System** — Config-tunable setpoints and limits

## Architecture Constraints

The automation layer is a **consumer of kernel services**, NOT a replacement for core IO:

| Constraint                                | Implementation                                                   |
| ----------------------------------------- | ---------------------------------------------------------------- |
| **BT nodes read via StateCache**          | No direct provider access; kernel remains single source of truth |
| **BT nodes act via CallRouter**           | All device calls go through validated control path               |
| **No new provider protocol features**     | Automation uses existing ADPP v0 capabilities                    |
| **No device-specific logic in BT engine** | BT runtime is capability-agnostic                                |

The BT engine sits **above** the kernel, not beneath it.

---

## Runtime Modes

### Mode State Machine

Anolis supports four runtime modes:

| Mode       | Description              | BT State | Manual Calls    |
| ---------- | ------------------------ | -------- | --------------- |
| **MANUAL** | Normal operator control  | Stopped  | Allowed         |
| **AUTO**   | Automated control active | Running  | Gated by policy |
| **IDLE**   | Standby mode             | Stopped  | Allowed         |
| **FAULT**  | Error recovery state     | Stopped  | Allowed         |

### Mode Transitions

Valid transitions:

```text
           ┌──────────┐
    ┌──────┤  MANUAL  ├──────┐
    │      └────┬─────┘      │
    │           │            │
    ▼           ▼            ▼
┌──────┐    ┌──────┐     ┌──────┐
│ IDLE │    │ AUTO │     │FAULT │
└──────┘    └──────┘     └───┬──┘
    │           │            │
    │           │            │
    └───────────┴────────────┘
        (All → FAULT allowed)
```

**Invalid transitions:**

- `FAULT → AUTO` — Must recover through MANUAL first
- `FAULT → IDLE` — Must recover through MANUAL first

**Rationale:** Fault recovery requires explicit operator acknowledgment before resuming automation.

### Default Mode

- **Startup:** MANUAL (safe default)
- **After fault:** FAULT (requires explicit recovery)
- **Config override:** Can set `runtime.mode: IDLE` in YAML

---

## Manual/Auto Contention Policy

When in AUTO mode, manual device calls are gated by the configured policy:

### BLOCK Policy (Default)

Manual calls are **rejected** when automation is active.

```yaml
automation:
  manual_gating_policy: BLOCK
```

**Behavior:**

- POST /v0/call returns `FAILED_PRECONDITION` error
- Logged as warning: "Manual call blocked in AUTO mode"
- BT continues running uninterrupted

**Use case:** Prevent accidental operator interference during automated sequences.

### OVERRIDE Policy

Manual calls are **allowed** when automation is active.

```yaml
automation:
  manual_gating_policy: OVERRIDE
```

**Behavior:**

- POST /v0/call succeeds normally
- Logged as info: "Manual call overriding AUTO mode"
- BT continues running (may conflict with manual action)

**Use case:** Allow expert operators to override automation when needed.

---

## HTTP API for Mode Control

### GET /v0/mode

Get current automation mode.

**Request:**

```http
GET /v0/mode HTTP/1.1
```

**Response (automation enabled):**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "mode": "MANUAL"
}
```

**Response (automation disabled):**

```json
{
  "status": {
    "code": "UNAVAILABLE",
    "message": "Automation layer not enabled"
  }
}
```

### POST /v0/mode

Set automation mode.

**Request:**

```http
POST /v0/mode HTTP/1.1
Content-Type: application/json

{
  "mode": "AUTO"
}
```

**Response (success):**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "mode": "AUTO"
}
```

**Response (invalid transition):**

```http
HTTP/1.1 412 Precondition Failed
Content-Type: application/json

{
  "status": {
    "code": "FAILED_PRECONDITION",
    "message": "Invalid mode transition: FAULT -> AUTO"
  }
}
```

**Response (invalid mode string):**

```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "status": {
    "code": "INVALID_ARGUMENT",
    "message": "Invalid mode: 'BADMODE' (must be MANUAL, AUTO, IDLE, or FAULT)"
  }
}
```

---

## Mode Change Events

Mode transitions emit telemetry events for observability.

**Event Type:** `mode_change`

**Fields:**

- `previous_mode` (string): Mode before transition
- `new_mode` (string): Mode after transition
- `timestamp_ms` (int64): Unix timestamp in milliseconds

**InfluxDB Line Protocol:**

```sh
mode_change previous_mode="MANUAL",new_mode="AUTO" 1706918400000
```

**Grafana Query (Flux):**

```flux
from(bucket: "anolis")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "mode_change")
  |> yield(name: "mode_transitions")
```

---

## Parameters

Anolis supports **runtime parameters** that can be declared in YAML and updated at runtime via HTTP.
Parameters are read-only from Behavior Trees (exposed via the blackboard) and can be validated with min/max ranges or enum allowed values.

### YAML Declaration

Example:

```yaml
automation:
  parameters:
    - name: temp_setpoint
      type: double
      default: 25.0
      min: 10.0
      max: 50.0

    - name: control_enabled
      type: bool
      default: true

    - name: operating_mode
      type: string
      default: "normal"
      allowed_values: ["normal", "test", "emergency"]
```

Notes:

- Supported types: `double`, `int64`, `bool`, `string`.
- `min`/`max` apply to numeric types only.
- `allowed_values` applies to string enums.
- **Persistence** (writing changes back to YAML) is **not implemented yet**.
- The intent is to keep this opt-in behind a future `automation.persist_parameters` flag.

### HTTP API for Parameters

GET /v0/parameters

- Returns the list of declared parameters with current values and constraints.

POST /v0/parameters

- Update a parameter at runtime.
- Body: `{"name": "temp_setpoint", "value": 30.0}`
- Server validates type and constraints and replies with 200 OK on success or 400/INVALID_ARGUMENT on validation failure.

Example (update):

```http
POST /v0/parameters HTTP/1.1
Content-Type: application/json

{"name": "temp_setpoint", "value": 30.0}
```

Example (response):

```json
{
  "status": { "code": "OK", "message": "ok" },
  "parameter": { "name": "temp_setpoint", "value": 30.0 }
}
```

### BT Access

Behavior Trees can access parameters via the `GetParameter` node.
The node is available in the default node registry and reads a parameter by name from the blackboard using the `param` input port.
It returns SUCCESS with the value available on the `value` output port.

Example:

```xml
<GetParameter param="temp_setpoint" value="{target_temp}"/>
```

---

## Configuration

### Enabling Automation

```yaml
automation:
  enabled: true
  behavior_tree: ./behaviors/demo.xml
  tick_rate_hz: 10 # 1-1000 Hz
  manual_gating_policy: BLOCK # BLOCK or OVERRIDE
```

### Configuration Fields

| Field                  | Type   | Default  | Description                            |
| ---------------------- | ------ | -------- | -------------------------------------- |
| `enabled`              | bool   | false    | Enable/disable automation layer        |
| `behavior_tree`        | string | required | Path to BT XML file                    |
| `tick_rate_hz`         | int    | 10       | BT execution rate (1-1000)             |
| `manual_gating_policy` | string | BLOCK    | Manual call policy (BLOCK or OVERRIDE) |
| `parameters`           | list   | []       | Parameter definitions                  |

---

## BT Execution Gating

The BT tick loop only runs when mode == AUTO:

```cpp
while (running) {
  if (mode != AUTO) {
    sleep(tick_period);
    continue;  // Skip tick
  }

  tree.tick();
  sleep(tick_period);
}
```

**Behavior:**

- MANUAL/IDLE/FAULT → BT paused cleanly (no state corruption)
- AUTO → BT ticks normally
- Transitioning to AUTO restarts BT from root node

---

## Dependency Notes

BehaviorTree.CPP is pulled via vcpkg (`behaviortree-cpp`). If vcpkg lags, the fallback is to vendor a known-good release from GitHub.
Link it in `core/CMakeLists.txt` and keep the same include paths to avoid code changes.
Keep the same include paths to avoid code changes.

## Usage Examples

### Starting Automated Control

```bash
# 1. Verify current mode
curl http://localhost:8080/v0/mode

# 2. Switch to AUTO mode
curl -X POST http://localhost:8080/v0/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"AUTO"}'

# 3. Monitor mode transitions in telemetry
# (Check Grafana or query InfluxDB)
```

### Emergency Stop

```bash
# Put system in FAULT mode (stops automation)
curl -X POST http://localhost:8080/v0/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"FAULT"}'

# Manually control devices
curl -X POST http://localhost:8080/v0/call \
  -H "Content-Type: application/json" \
  -d '{
    "provider_id": "sim0",
    "device_id": "tempctl0",
    "function_name": "set_target_temp",
    "arguments": {"target": 20.0}
  }'
```

### Recovery from FAULT

```bash
# 1. Recover to MANUAL
curl -X POST http://localhost:8080/v0/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"MANUAL"}'

# 2. Verify system state
curl http://localhost:8080/v0/state

# 3. Resume automation
curl -X POST http://localhost:8080/v0/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"AUTO"}'
```

---

## Operator UI Integration (Planned)

The Operator UI does not yet expose mode or parameter controls. When UI work begins, use these endpoints:

- `GET /v0/mode` and `POST /v0/mode` for mode control
- `GET /v0/parameters` and `POST /v0/parameters` for runtime parameter tuning

## Testing

Automated tests for automation features:

```bash
# Run automation tests
python scripts/test_automation.py

# Test specific scenarios
python scripts/test_automation.py --port 18080
```

**Test coverage:**

- Mode transitions (valid and invalid)
- Manual/auto contention policy
- BT execution gating
- Mode change events
- Automation disabled behavior

---

## Safety Disclaimer

⚠️ **Automation is a control policy layer, not a safety-rated system.**

External safety systems (e.g., E-stops, interlocks) are **still required** for real hardware.

- FAULT mode is _policy_, not a certified safety mechanism
- Mode transitions are not safety-rated
- Manual override capability must not replace proper safety interlocks

For production deployment:

1. Integrate with certified safety PLCs
2. Implement hardware E-stops independent of Anolis
3. Add watchdog timers for automation health
4. Design BTs with safe failure modes

---

## References

- [BehaviorTree.CPP Documentation](https://www.behaviortree.dev/)
- [HTTP API Documentation](./http-api.md)
- [Architecture Overview](./architecture.md)
