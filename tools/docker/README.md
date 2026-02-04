# Anolis Observability Stack (Phase 6B)

This directory contains Docker configuration for the Anolis telemetry stack:

- **InfluxDB 2.7**: Time-series database for signal history
- **Grafana 10.3.1**: Visualization dashboards

## Quick Start

### 1. Start the Stack

```bash
# Create .env from template (optional - defaults work for local dev)
cp .env.example .env

# Start containers
docker compose -f docker-compose.observability.yml up -d

# Verify containers are running
docker compose -f docker-compose.observability.yml ps
```

### 2. Access Services

| Service  | URL                     | Credentials       |
| -------- | ----------------------- | ----------------- |
| InfluxDB | <http://localhost:8086> | admin / anolis123 |
| Grafana  | <http://localhost:3000> | admin / anolis123 |

### 3. Run Anolis with Telemetry

```bash
# From anolis root directory
./build/core/Release/anolis-runtime.exe anolis-runtime-telemetry.yaml
```

### 4. View Dashboards

1. Open <http://localhost:3000> (Grafana)
2. Login with admin / anolis123
3. Navigate to **Dashboards** → **Anolis**
4. Select **Signal History** or **Device Health**

## Configuration

### Environment Variables

Copy `.env.example` to `.env` and customize:

| Variable          | Default   | Description             |
| ----------------- | --------- | ----------------------- |
| INFLUXDB_USERNAME | admin     | InfluxDB admin user     |
| INFLUXDB_PASSWORD | anolis123 | InfluxDB admin password |
| INFLUXDB_ORG      | anolis    | InfluxDB organization   |
| INFLUXDB_BUCKET   | anolis    | InfluxDB bucket name    |
| INFLUXDB_TOKEN    | dev-token | API token for writes    |
| GRAFANA_PASSWORD  | anolis123 | Grafana admin password  |

### Runtime Config

In your `anolis-runtime.yaml`, enable telemetry:

```yaml
telemetry:
  enabled: true
  influxdb:
    url: http://localhost:8086
    org: anolis
    bucket: anolis
    token: dev-token # Or use INFLUXDB_TOKEN env var
    batch_size: 100
    flush_interval_ms: 1000
```

## Dashboards

### Signal History

Time-series visualization of signal values:

- **Double values**: Temperature, pressure, duty cycles
- **Boolean values**: Switch states, enabled flags
- **Integer values**: RPM, counts

Supports filtering by:

- Provider
- Device
- Signal

### Device Health

Status overview of all devices:

- **Quality status**: OK / STALE / ERROR counts
- **Quality table**: Per-signal quality status
- **Staleness gauge**: Time since last update

## Data Schema

InfluxDB measurement: `anolis_signal`

**Tags**:

- `provider_id`: Provider identifier (e.g., "sim0")
- `device_id`: Device identifier (e.g., "tempctl-0")
- `signal_id`: Signal identifier (e.g., "temperature")

**Fields**:

- `value_double`: Double values
- `value_int`: Integer values
- `value_bool`: Boolean values
- `quality`: Signal quality ("OK", "STALE", etc.)

**Example line protocol**:

```flux
anolis_signal,provider_id=sim0,device_id=tempctl-0,signal_id=temperature value_double=23.5,quality="OK" 1706960096000
```

## Troubleshooting

### InfluxDB Not Starting

Check health status:

```bash
docker compose -f docker-compose.observability.yml logs influxdb
```

### No Data in Grafana

1. Verify runtime is writing to InfluxDB:

   ```
   [InfluxSink] Written 1000 events to InfluxDB
   ```

2. Check InfluxDB data explorer:
   - Open http://localhost:8086
   - Go to Data Explorer
   - Query `anolis_signal` measurement

3. Verify Grafana datasource connection:
   - Settings → Data Sources → InfluxDB
   - Click "Test" button

### Dashboards Not Appearing

Dashboards are auto-provisioned from `../grafana/dashboards/`.
If not appearing:

```bash
docker compose -f docker-compose.observability.yml restart grafana
```

## Cleanup

```bash
# Stop containers
docker compose -f docker-compose.observability.yml down

# Remove volumes (deletes all data)
docker compose -f docker-compose.observability.yml down -v
```
