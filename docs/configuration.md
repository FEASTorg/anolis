# Configuration

Anolis Runtime is configured via a YAML file (default: `anolis-runtime.yaml`).

> **📖 Complete Schema Reference:**
> See [configuration-schema.md](configuration-schema.md) for the canonical v1.0 schema with validation rules, constraints, and migration notes.

## Structure

```yaml
runtime:
  mode: MANUAL # MANUAL | AUTO | IDLE | FAULT

http:
  enabled: true
  bind: 127.0.0.1
  port: 8080
  # CORS allowlist ("*" = allow all; recommended to pin your UI origin)
  cors_allowed_origins:
    - http://localhost:3000
    - http://127.0.0.1:3000
  cors_allow_credentials: false
  thread_pool_size: 40 # Worker threads (default: 40)

polling:
  interval_ms: 500 # Device polling interval

logging:
  level: info # debug, info, warn, error

telemetry:
  enabled: false
  # ... (InfluxDB settings)

providers:
  - id: sim0
    command: ./path/to/provider
    timeout_ms: 5000
    args: []

automation:
  enabled: false
  behavior_tree: path/to/tree.xml
  tick_rate_hz: 10
```

## Options

### HTTP

- **bind/port**: Interface and port to listen on.
- **cors_allowed_origins**: List of allowed origins. Use `"*"` only for development; pin exact origins in validation/production.
- **cors_allow_credentials**: Emit `Access-Control-Allow-Credentials: true` when your UI needs cookies/auth headers.
- **thread_pool_size**: Adjust based on concurrent SSE clients. Formula: `max_sse_clients + 8`.

### Logging

- **level**: Controls verbosity. Use `debug` for troubleshooting provider issues.

### Providers

- **id**: Unique provider identifier
- **command**: Path to provider executable
- **args**: Command-line arguments
- **timeout_ms**: ADPP operation timeout (default: 5000ms)
- **restart_policy**: Automatic supervision configuration (optional)

#### Restart Policy

Automatic supervision monitors provider health and restarts crashed providers:

```yaml
providers:
  - id: hardware
    command: ./my-provider
    restart_policy:
      enabled: true
      max_attempts: 3
      backoff_ms: [200, 500, 1000]
      timeout_ms: 30000
```

- **enabled**: Enable automatic restart (default: false)
- **max_attempts**: Restart attempts before circuit breaker opens (default: 3)
- **backoff_ms**: Delay before each restart attempt in milliseconds. Array length must match max_attempts.
- **timeout_ms**: Maximum time to wait for restart (default: 30000ms)

**Behavior:**

- Provider crashes → supervisor records crash → waits backoff delay → attempts restart
- Successful restart → crash counter resets
- Circuit breaker opens after max_attempts → no further restarts until manual intervention
- Devices cleared before restart, rediscovered after successful restart

### Telemetry (InfluxDB)

- **enabled**: Enable/disable telemetry sink
- **url**: InfluxDB server URL (supports both HTTP and HTTPS)
- **org**: InfluxDB organization name
- **bucket**: InfluxDB bucket name
- **token**: InfluxDB API token (required when enabled)

Both HTTP and HTTPS URLs are supported:

```yaml
# Local development
url: http://localhost:8086

# Cloud/production with TLS
url: https://influx.example.com:8086
```

**TLS Certificate Verification:** HTTPS connections verify server certificates using the system's default CA store.
For self-signed certificates, either add them to the system trust store or use HTTP with a reverse proxy.
