# Configuration

Anolis Runtime is configured via a YAML file (default: `anolis-runtime.yaml`).

## Structure

```yaml
runtime:
  mode: MANUAL # MANUAL | AUTOMATED

http:
  enabled: true
  bind: 127.0.0.1
  port: 8080
  cors_origin: "*"        # Allowed Origin for CORS (default: *)
  thread_pool_size: 40    # Worker threads (default: 40)

polling:
  interval_ms: 500        # Device polling interval

logging:
  level: info             # debug, info, warn, error

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
- **cors_origin**: Set to strictly match your UI origin (e.g. `http://localhost:3000`) in trusted environments.
- **thread_pool_size**: Adjust based on concurrent SSE clients. Formula: `max_sse_clients + 8`.

### Logging

- **level**: Controls verbosity. Use `debug` for troubleshooting provider issues.
