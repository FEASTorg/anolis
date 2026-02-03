# Testing Phase 6B Telemetry (WSL2)

Quick guide for testing the Anolis telemetry stack on Windows with WSL2.

## Prerequisites

- Docker Desktop for Windows with WSL2 backend
- Anolis runtime built (Windows native)
- WSL2 distro (Ubuntu recommended)

## Step 1: Start Observability Stack (WSL2)

```bash
# In WSL2 terminal
cd /mnt/d/repos_feast/anolis/tools/docker

# Start InfluxDB + Grafana
docker compose -f docker-compose.observability.yml up -d

# Verify both containers are healthy
docker compose -f docker-compose.observability.yml ps
```

Expected output:

```
NAME              STATUS                   PORTS
anolis-grafana    Up About a minute        0.0.0.0:3000->3000/tcp
anolis-influxdb   Up About a minute (healthy)  0.0.0.0:8086->8086/tcp
```

## Step 2: Run Anolis Runtime (Windows)

In **PowerShell/CMD** (not WSL):

```powershell
cd D:\repos_feast\anolis
.\build\core\Release\anolis-runtime.exe anolis-runtime-telemetry.yaml
```

Expected output includes:

```
[Runtime] Telemetry sink started
[InfluxSink] Started, writing to http://localhost:8086/anolis
```

## Step 3: Verify Data Flow

### Check InfluxDB is receiving data

Open http://localhost:8086 in browser:

1. Login: `admin` / `anolis123`
2. Go to **Data Explorer** (left sidebar)
3. Select bucket: `anolis`
4. Query: `anolis_signal`
5. Click **Submit** - should see data points

### Check Grafana dashboards

Open http://localhost:3000 in browser:

1. Login: `admin` / `anolis123`
2. Go to **Dashboards** → **Browse**
3. Open **Signal History** or **Device Health**
4. Data should appear within 5-10 seconds

## Step 4: Quick Validation Checklist

| Check                                        | Expected |
| -------------------------------------------- | -------- |
| `[InfluxSink] Started` in runtime output     | ✓        |
| `[InfluxSink] Written X events` logs appear  | ✓        |
| InfluxDB Data Explorer shows `anolis_signal` | ✓        |
| Grafana Signal History shows time-series     | ✓        |
| Grafana Device Health shows status table     | ✓        |

## Cleanup

```bash
# In WSL2
cd /mnt/d/repos_feast/anolis/tools/docker
docker compose -f docker-compose.observability.yml down

# To also remove data volumes:
docker compose -f docker-compose.observability.yml down -v
```

## Troubleshooting

### "Connection refused" from runtime

InfluxDB not ready yet. Wait for health check:

```bash
docker compose -f docker-compose.observability.yml logs influxdb
```

### No data in Grafana

1. Check runtime shows `[InfluxSink] Written` logs
2. Verify time range in Grafana (top right) is "Last 15 minutes"
3. Try refreshing dashboard (Ctrl+R)

### WSL2 networking issues

Docker Desktop should expose ports to Windows automatically.
If not, try restarting Docker Desktop.

### Dashboards not appearing

```bash
docker compose -f docker-compose.observability.yml restart grafana
```

## Credentials Reference

| Service        | URL                   | User  | Password  |
| -------------- | --------------------- | ----- | --------- |
| InfluxDB       | http://localhost:8086 | admin | anolis123 |
| Grafana        | http://localhost:3000 | admin | anolis123 |
| InfluxDB Token | N/A                   | N/A   | dev-token |
