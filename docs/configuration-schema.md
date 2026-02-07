# Configuration Schema v1.0

**Status:** Canonical  
**Date:** February 6, 2026

This document defines the canonical YAML configuration schema for Anolis Runtime v0.

---

## Top-Level Structure

```yaml
runtime:
  mode: <string>

http:
  enabled: <bool>
  bind: <string>
  port: <int>
  cors_allowed_origins: <string|array>
  cors_allow_credentials: <bool>
  thread_pool_size: <int>

providers:
  - id: <string>
    command: <string>
    args: <array>
    timeout_ms: <int>

polling:
  interval_ms: <int>

telemetry:
  enabled: <bool>
  influxdb:
    url: <string>
    org: <string>
    bucket: <string>
    token: <string>
    batch_size: <int>
    flush_interval_ms: <int>

logging:
  level: <string>

automation:
  enabled: <bool>
  behavior_tree: <string>
  tick_rate_hz: <int>
  manual_gating_policy: <string>
  parameters:
    - name: <string>
      type: <string>
      default: <varies>
      min: <number>
      max: <number>
      allowed_values: <array>
```

---

## Section: `runtime`

**Required:** Yes

### `runtime.mode`

- **Type:** `string`
- **Required:** No
- **Default:** `MANUAL`
- **Valid Values:** `MANUAL`, `AUTO`
- **Description:** Initial runtime mode. In v0, only `MANUAL` is fully supported.

**Example:**

```yaml
runtime:
  mode: MANUAL
```

---

## Section: `http`

**Required:** No (but HTTP server won't start if omitted)

### `http.enabled`

- **Type:** `bool`
- **Required:** No
- **Default:** `true`
- **Description:** Whether to start the HTTP API server.

### `http.bind`

- **Type:** `string`
- **Required:** No
- **Default:** `127.0.0.1`
- **Description:** IP address to bind HTTP server to. Use `0.0.0.0` to listen on all interfaces.

### `http.port`

- **Type:** `int`
- **Required:** No
- **Default:** `8080`
- **Constraints:** Must be between 1 and 65535
- **Description:** TCP port for HTTP server.

### `http.cors_allowed_origins`

- **Type:** `string` or `array of strings`
- **Required:** No
- **Default:** `["*"]` (allow all origins)
- **Description:** CORS allowed origins. Use `"*"` to allow all, or list specific origins.

**Examples:**

```yaml
# Allow all
http:
  cors_allowed_origins: "*"

# Allow specific origins
http:
  cors_allowed_origins:
    - http://localhost:3000
    - http://127.0.0.1:3000
```

### `http.cors_allow_credentials`

- **Type:** `bool`
- **Required:** No
- **Default:** `false`
- **Description:** Whether to include `Access-Control-Allow-Credentials` header.

### `http.thread_pool_size`

- **Type:** `int`
- **Required:** No
- **Default:** `40`
- **Constraints:** Must be at least 1
- **Description:** Size of HTTP worker thread pool.

**Example:**

```yaml
http:
  enabled: true
  bind: 127.0.0.1
  port: 8080
  cors_allowed_origins:
    - http://localhost:3000
  cors_allow_credentials: false
  thread_pool_size: 40
```

---

## Section: `providers`

**Required:** Yes (at least one provider)

Array of provider configurations. Each provider is a separate executable that implements the ADPP protocol.

### `providers[].id`

- **Type:** `string`
- **Required:** Yes
- **Description:** Unique identifier for this provider. Used in device paths (`provider_id/device_id`).

### `providers[].command`

- **Type:** `string`
- **Required:** Yes
- **Description:** Absolute or relative path to provider executable.

### `providers[].args`

- **Type:** `array of strings`
- **Required:** No
- **Default:** `[]`
- **Description:** Command-line arguments passed to provider executable.

### `providers[].timeout_ms`

- **Type:** `int`
- **Required:** No
- **Default:** `5000`
- **Constraints:** Must be at least 100
- **Description:** Timeout for ADPP operations (in milliseconds).

**Example:**

```yaml
providers:
  - id: sim0
    command: ./build/anolis-provider-sim.exe
    args: []
    timeout_ms: 5000

  - id: hardware
    command: /opt/providers/hardware-provider
    args: ["--port", "/dev/ttyUSB0"]
    timeout_ms: 3000
```

---

## Section: `polling`

**Required:** No

### `polling.interval_ms`

- **Type:** `int`
- **Required:** No
- **Default:** `500`
- **Constraints:** Must be at least 100
- **Description:** State cache polling interval (in milliseconds). All device signals are polled at this rate.

**Example:**

```yaml
polling:
  interval_ms: 500
```

---

## Section: `telemetry`

**Required:** No

### `telemetry.enabled`

- **Type:** `bool`
- **Required:** No
- **Default:** `false`
- **Description:** Whether to enable telemetry data export to InfluxDB.

### `telemetry.influxdb`

**Required:** Only if `telemetry.enabled` is `true`

Nested configuration for InfluxDB connection.

#### `telemetry.influxdb.url`

- **Type:** `string`
- **Required:** No
- **Default:** `http://localhost:8086`
- **Description:** InfluxDB server URL.

#### `telemetry.influxdb.org`

- **Type:** `string`
- **Required:** No
- **Default:** `anolis`
- **Description:** InfluxDB organization name.

#### `telemetry.influxdb.bucket`

- **Type:** `string`
- **Required:** No
- **Default:** `anolis`
- **Description:** InfluxDB bucket name where data will be written.

#### `telemetry.influxdb.token`

- **Type:** `string`
- **Required:** No (can use `INFLUXDB_TOKEN` environment variable)
- **Default:** (empty, checked from environment)
- **Description:** InfluxDB API token for authentication. If not provided, reads from `INFLUXDB_TOKEN` environment variable.

#### `telemetry.influxdb.batch_size`

- **Type:** `int`
- **Required:** No
- **Default:** `100`
- **Description:** Number of data points to batch before flushing to InfluxDB.

#### `telemetry.influxdb.flush_interval_ms`

- **Type:** `int`
- **Required:** No
- **Default:** `1000`
- **Description:** Maximum time (in milliseconds) to wait before flushing batched data.

**Example:**

```yaml
telemetry:
  enabled: true
  influxdb:
    url: http://localhost:8086
    org: anolis
    bucket: anolis
    token: my-secret-token # Or use INFLUXDB_TOKEN env var
    batch_size: 100
    flush_interval_ms: 1000
```

---

## Section: `logging`

**Required:** No

### `logging.level`

- **Type:** `string`
- **Required:** No
- **Default:** `info`
- **Valid Values:** `debug`, `info`, `warn`, `error`
- **Description:** Minimum log level to output.

**Example:**

```yaml
logging:
  level: info
```

---

## Section: `automation`

**Required:** No

### `automation.enabled`

- **Type:** `bool`
- **Required:** No
- **Default:** `false`
- **Description:** Whether to enable behavior tree automation.

### `automation.behavior_tree`

- **Type:** `string`
- **Required:** Yes (if `automation.enabled` is `true`)
- **Description:** Path to BehaviorTree.CPP XML file describing the automation logic.

### `automation.tick_rate_hz`

- **Type:** `int`
- **Required:** No
- **Default:** `10`
- **Constraints:** Must be between 1 and 1000
- **Description:** Behavior tree tick rate (in Hertz).

### `automation.manual_gating_policy`

- **Type:** `string`
- **Required:** No
- **Default:** `BLOCK`
- **Valid Values:** `BLOCK`, `OVERRIDE`
- **Description:** How automation handles calls when runtime is in MANUAL mode. `BLOCK` rejects automation calls, `OVERRIDE` allows them.

### `automation.parameters`

**Required:** No

Array of runtime parameters that can be modified via HTTP API and accessed in behavior trees.

#### `automation.parameters[].name`

- **Type:** `string`
- **Required:** Yes
- **Description:** Unique parameter name.

#### `automation.parameters[].type`

- **Type:** `string`
- **Required:** Yes
- **Valid Values:** `double`, `int64`, `bool`, `string`
- **Description:** Parameter data type.

#### `automation.parameters[].default`

- **Type:** Varies based on `type`
- **Required:** No
- **Description:** Initial parameter value.

#### `automation.parameters[].min`

- **Type:** `number`
- **Required:** No (only for `double` and `int64` types)
- **Description:** Minimum allowed value.

#### `automation.parameters[].max`

- **Type:** `number`
- **Required:** No (only for `double` and `int64` types)
- **Description:** Maximum allowed value.

#### `automation.parameters[].allowed_values`

- **Type:** `array of strings`
- **Required:** No (only for `string` type)
- **Description:** Enumeration of valid string values.

**Example:**

```yaml
automation:
  enabled: true
  behavior_tree: ./behaviors/demo.xml
  tick_rate_hz: 10
  manual_gating_policy: BLOCK
  parameters:
    - name: temp_setpoint
      type: double
      default: 25.0
      min: 10.0
      max: 50.0

    - name: motor_duty_cycle
      type: int64
      default: 50
      min: 0
      max: 100

    - name: control_enabled
      type: bool
      default: true

    - name: operating_mode
      type: string
      default: "normal"
      allowed_values: ["normal", "test", "emergency"]
```

---

## Complete Example

```yaml
# Anolis Runtime Configuration v1.0

runtime:
  mode: MANUAL

http:
  enabled: true
  bind: 127.0.0.1
  port: 8080
  cors_allowed_origins:
    - http://localhost:3000
    - http://127.0.0.1:3000
  cors_allow_credentials: false

providers:
  - id: sim0
    command: ./anolis-provider-sim.exe
    args: []
    timeout_ms: 5000

polling:
  interval_ms: 500

telemetry:
  enabled: true
  influxdb:
    url: http://localhost:8086
    org: anolis
    bucket: anolis
    token: dev-token
    batch_size: 100
    flush_interval_ms: 1000

logging:
  level: info

automation:
  enabled: true
  behavior_tree: ./behaviors/demo.xml
  tick_rate_hz: 10
  manual_gating_policy: BLOCK
  parameters:
    - name: temp_setpoint
      type: double
      default: 25.0
      min: 10.0
      max: 50.0
```

---

## Schema Validation

The runtime validates all configuration fields at startup. Invalid configurations will cause the runtime to exit with a clear error message.

### Unknown Keys

**Behavior:** Unknown top-level keys will generate warnings but won't prevent startup.
This allows for forwards compatibility with future schema versions.

### Migration from v0 Flat Keys

**Note:** Version 0 of the runtime accepted flat telemetry keys (`influx_url`, `influx_org`, etc.) at the `telemetry` level.
These are **deprecated** in v1.0. Use the nested `telemetry.influxdb.*` structure instead.

**Deprecated (v0):**

```yaml
telemetry:
  enabled: true
  influx_url: http://localhost:8086
  influx_org: anolis
```

**Canonical (v1.0):**

```yaml
telemetry:
  enabled: true
  influxdb:
    url: http://localhost:8086
    org: anolis
```

---

## Environment Variables

The following environment variables are supported:

### `INFLUXDB_TOKEN`

If `telemetry.influxdb.token` is not specified in config file, the runtime will attempt to read the InfluxDB API token from this env variable.

**Example:**

```bash
export INFLUXDB_TOKEN="your-secret-token"
./anolis-runtime --config=anolis-runtime.yaml
```
