# Anolis HTTP API Reference (v0)

This document describes the HTTP REST API exposed by `anolis-runtime` for device discovery, state monitoring, and control.

## Overview

- **Base URL**: `http://127.0.0.1:8080` (configurable)
- **Content-Type**: `application/json`
- **API Version**: `v0` (all endpoints prefixed with `/v0`)

## Authentication

No authentication in v0. The server binds to localhost only by default.

## CORS

The server includes CORS headers for browser-based clients:

- `Access-Control-Allow-Origin`: Configurable via `http.cors_allowed_origins` (default: `[*]`)
- `Access-Control-Allow-Methods`: `GET, POST, OPTIONS`
- `Access-Control-Allow-Headers`: `Content-Type`

This allows the Operator UI (`tools/operator-ui/`) to connect from any origin by default.

> **Note**: For production/validation, restrict `http.cors_allowed_origins` in `anolis-runtime.yaml`.

## Response Format

All responses include a `status` object:

```json
{
  "status": {
    "code": "OK",
    "message": "ok"
  }
  // ... endpoint-specific data
}
```

### Status Codes

| Code                  | HTTP Status | Description                       |
| --------------------- | ----------- | --------------------------------- |
| `OK`                  | 200         | Success                           |
| `INVALID_ARGUMENT`    | 400         | Bad request or invalid parameters |
| `NOT_FOUND`           | 404         | Resource not found                |
| `FAILED_PRECONDITION` | 409         | Precondition not met              |
| `UNAVAILABLE`         | 503         | Provider or device unavailable    |
| `DEADLINE_EXCEEDED`   | 504         | Request timeout                   |
| `INTERNAL`            | 500         | Internal server error             |

---

## Endpoints

### GET /v0/runtime/status

Get runtime status, mode, and provider health.

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "mode": "MANUAL",
  "uptime_seconds": 3600,
  "polling_interval_ms": 500,
  "device_count": 2,
  "providers": [
    {
      "provider_id": "sim0",
      "state": "AVAILABLE",
      "device_count": 2
    }
  ]
}
```

**Fields:**

| Field                 | Type    | Description                                        |
| --------------------- | ------- | -------------------------------------------------- |
| `mode`                | string  | Runtime mode: `MANUAL`, `AUTO`, `IDLE`, or `FAULT` |
| `uptime_seconds`      | integer | Seconds since runtime started                      |
| `polling_interval_ms` | integer | State polling interval                             |
| `device_count`        | integer | Total devices across all providers                 |
| `providers`           | array   | Provider status list                               |
| `providers[].state`   | string  | `AVAILABLE`, `UNAVAILABLE`, `STARTING`, `CRASHED`  |

---

### GET /v0/mode

Get current automation mode and manual gating policy.

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "mode": "MANUAL",
  "policy": "BLOCK"
}
```

---

### POST /v0/mode

Set automation mode.

**Request:**

```json
{ "mode": "AUTO" }
```

**Response:**

```json
{ "status": { "code": "OK", "message": "ok" }, "mode": "AUTO" }
```

---

### GET /v0/parameters

List all runtime parameters with current values and constraints.

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "parameters": [
    {
      "name": "temp_setpoint",
      "type": "double",
      "value": 25.0,
      "min": 10.0,
      "max": 50.0
    }
  ]
}
```

---

### POST /v0/parameters

Update a runtime parameter (validated against constraints).

**Request:**

```json
{ "name": "temp_setpoint", "value": 30.0 }
```

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "parameter": { "name": "temp_setpoint", "value": 30.0 }
}
```

---

### GET /v0/automation/tree

Get the loaded behavior tree XML content.

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "tree": "<root main_tree_to_execute=\"MainTree\">\n  <BehaviorTree ID=\"MainTree\">...</BehaviorTree>\n</root>"
}
```

**Error Responses:**

- `UNAVAILABLE` - Automation layer not enabled
- `NOT_FOUND` - No behavior tree loaded
- `INTERNAL` - Failed to read behavior tree file

---

### GET /v0/devices

List all discovered devices.

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "devices": [
    {
      "provider_id": "sim0",
      "device_id": "tempctl0",
      "type": "tempctl"
    },
    {
      "provider_id": "sim0",
      "device_id": "motorctl0",
      "type": "motorctl"
    }
  ]
}
```

**Fields:**

| Field                   | Type   | Description                                |
| ----------------------- | ------ | ------------------------------------------ |
| `devices[].provider_id` | string | Provider that owns this device             |
| `devices[].device_id`   | string | Unique device identifier (within provider) |
| `devices[].type`        | string | Device type identifier                     |

---

### GET /v0/devices/{provider_id}/{device_id}/capabilities

Get device capabilities (signals and functions).

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "provider_id": "sim0",
  "device_id": "tempctl0",
  "capabilities": {
    "signals": [
      {
        "signal_id": "tc1_temp",
        "label": "TC1 Temperature",
        "value_type": "double"
      },
      {
        "signal_id": "relay1_state",
        "label": "Relay 1 State",
        "value_type": "bool"
      },
      {
        "signal_id": "control_mode",
        "label": "Control Mode",
        "value_type": "string"
      }
    ],
    "functions": [
      {
        "function_id": 1,
        "name": "set_mode",
        "label": "Set control mode: open or closed",
        "args": {
          "mode": {}
        }
      },
      {
        "function_id": 2,
        "name": "set_setpoint",
        "label": "Set closed-loop setpoint (C)",
        "args": {
          "value": {}
        }
      }
    ]
  }
}
```

**Signal Fields:**

| Field        | Type   | Description                                                        |
| ------------ | ------ | ------------------------------------------------------------------ |
| `signal_id`  | string | Signal identifier                                                  |
| `label`      | string | Human-readable label                                               |
| `value_type` | string | Value type: `double`, `int64`, `uint64`, `bool`, `string`, `bytes` |

**Function Fields:**

| Field         | Type    | Description                            |
| ------------- | ------- | -------------------------------------- |
| `function_id` | integer | Numeric function ID for calls          |
| `name`        | string  | Function name                          |
| `label`       | string  | Human-readable description             |
| `args`        | object  | Argument names (values are type hints) |

---

### GET /v0/state

Get latest cached state for all devices.

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "generated_at_epoch_ms": 1730000000000,
  "devices": [
    {
      "provider_id": "sim0",
      "device_id": "tempctl0",
      "quality": "OK",
      "values": [
        {
          "signal_id": "tc1_temp",
          "value": { "type": "double", "double": 23.5 },
          "quality": "OK",
          "timestamp_epoch_ms": 1730000000000,
          "age_ms": 150
        },
        {
          "signal_id": "relay1_state",
          "value": { "type": "bool", "bool": false },
          "quality": "OK",
          "timestamp_epoch_ms": 1730000000000,
          "age_ms": 150
        }
      ]
    }
  ]
}
```

**Device Fields:**

| Field     | Type   | Description                               |
| --------- | ------ | ----------------------------------------- |
| `quality` | string | Overall device quality (worst of signals) |

**Value Fields:**

| Field                | Type    | Description                                           |
| -------------------- | ------- | ----------------------------------------------------- |
| `signal_id`          | string  | Signal identifier                                     |
| `value`              | object  | Typed value (see Value Types below)                   |
| `quality`            | string  | Signal quality: `OK`, `STALE`, `UNAVAILABLE`, `FAULT` |
| `timestamp_epoch_ms` | integer | When value was polled (Unix ms)                       |
| `age_ms`             | integer | Milliseconds since poll                               |

---

### GET /v0/state/{provider_id}/{device_id}

Get state for a single device.

**Response:**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "generated_at_epoch_ms": 1730000000000,
  "provider_id": "sim0",
  "device_id": "motorctl0",
  "quality": "OK",
  "values": [
    {
      "signal_id": "motor1_duty",
      "value": { "type": "double", "double": 0.75 },
      "quality": "OK",
      "timestamp_epoch_ms": 1730000000000,
      "age_ms": 50
    }
  ]
}
```

---

### POST /v0/call

Execute a device function.

**Request:**

```json
{
  "provider_id": "sim0",
  "device_id": "motorctl0",
  "function_id": 10,
  "args": {
    "motor_index": { "type": "int64", "int64": 1 },
    "duty": { "type": "double", "double": 0.75 }
  }
}
```

**Request Fields:**

| Field         | Type    | Required | Description                       |
| ------------- | ------- | -------- | --------------------------------- |
| `provider_id` | string  | Yes      | Target provider                   |
| `device_id`   | string  | Yes      | Target device                     |
| `function_id` | integer | Yes      | Function ID from capabilities     |
| `args`        | object  | No       | Function arguments (typed values) |

**Response (Success):**

```json
{
  "status": { "code": "OK", "message": "ok" },
  "provider_id": "sim0",
  "device_id": "motorctl0",
  "function_id": 10,
  "post_call_poll_triggered": true
}
```

**Response (Error):**

```json
{
  "status": {
    "code": "INVALID_ARGUMENT",
    "message": "Provider returned error: motor_index must be 1 or 2"
  }
}
```

---

## Value Types

All values use a typed JSON encoding:

| Type     | JSON Format                               | Example                 |
| -------- | ----------------------------------------- | ----------------------- |
| `double` | `{"type": "double", "double": 1.23}`      | Temperature, duty cycle |
| `int64`  | `{"type": "int64", "int64": -42}`         | Signed integers         |
| `uint64` | `{"type": "uint64", "uint64": 12345}`     | Counters, timestamps    |
| `bool`   | `{"type": "bool", "bool": true}`          | Relay states, flags     |
| `string` | `{"type": "string", "string": "open"}`    | Mode, status text       |
| `bytes`  | `{"type": "bytes", "base64": "AAECAw=="}` | Binary data (base64)    |

---

## Quality Values

| Quality       | Description                        |
| ------------- | ---------------------------------- |
| `OK`          | Fresh data, recently polled        |
| `STALE`       | Provider reachable but data is old |
| `UNAVAILABLE` | Provider or device unreachable     |
| `FAULT`       | Device-reported fault condition    |

Device-level quality is the worst-case of its signal qualities.

---

## Error Responses

All errors follow the same format:

```json
{
  "status": {
    "code": "NOT_FOUND",
    "message": "Device not found: sim0/nonexistent"
  }
}
```

### Common Errors

| Scenario               | HTTP | Code                |
| ---------------------- | ---- | ------------------- |
| Unknown route          | 404  | `NOT_FOUND`         |
| Unknown device         | 404  | `NOT_FOUND`         |
| Invalid JSON body      | 400  | `INVALID_ARGUMENT`  |
| Missing required field | 400  | `INVALID_ARGUMENT`  |
| Provider unavailable   | 503  | `UNAVAILABLE`       |
| Call timeout           | 504  | `DEADLINE_EXCEEDED` |
| Internal error         | 500  | `INTERNAL`          |

---

## Configuration

HTTP server is configured in `anolis-runtime.yaml`:

```yaml
http:
  enabled: true # Enable HTTP server (default: true)
  bind: 127.0.0.1 # Bind address (default: 127.0.0.1)
  port: 8080 # Port (default: 8080)
```

---

## Curl Examples

### List devices

```bash
curl -s http://127.0.0.1:8080/v0/devices | jq
```

### Get device capabilities

```bash
curl -s http://127.0.0.1:8080/v0/devices/sim0/tempctl0/capabilities | jq
```

### Get all state

```bash
curl -s http://127.0.0.1:8080/v0/state | jq
```

### Get single device state

```bash
curl -s http://127.0.0.1:8080/v0/state/sim0/motorctl0 | jq
```

### Set motor duty

```bash
curl -s -X POST http://127.0.0.1:8080/v0/call \
  -H "Content-Type: application/json" \
  -d '{
    "provider_id": "sim0",
    "device_id": "motorctl0",
    "function_id": 10,
    "args": {
      "motor_index": {"type": "int64", "int64": 1},
      "duty": {"type": "double", "double": 0.75}
    }
  }' | jq
```

### Set temperature setpoint

```bash
curl -s -X POST http://127.0.0.1:8080/v0/call \
  -H "Content-Type: application/json" \
  -d '{
    "provider_id": "sim0",
    "device_id": "tempctl0",
    "function_id": 2,
    "args": {
      "value": {"type": "double", "double": 50.0}
    }
  }' | jq
```

### Get runtime status

```bash
curl -s http://127.0.0.1:8080/v0/runtime/status | jq
```
