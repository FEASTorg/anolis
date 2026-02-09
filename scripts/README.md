# Scripts

Quick reference for all automation scripts in the repository.

## Core Scripts (scripts/)

| Script           | Purpose                                          | Usage                            |
| ---------------- | ------------------------------------------------ | -------------------------------- |
| **dev.ps1/sh**   | **One-command dev environment**                  | `.\scripts\dev.ps1`              |
|                  | Starts: Runtime + UI + Docker (InfluxDB/Grafana) | Recommended for Phase 14 testing |
|                  | Options: `-SkipInfra`, `-NoUI`, `-SkipBuild`     |                                  |
| **build.ps1/sh** | Build anolis + anolis-provider-sim               | `.\scripts\build.ps1`            |
|                  | Options: `--Clean`, `--Release`, `--Debug`       |                                  |
| **run.ps1/sh**   | Start anolis runtime only                        | `.\scripts\run.ps1`              |
|                  | Options: `-Config PATH`, `-BuildDir PATH`        | Production/CI usage              |
| **test.ps1/sh**  | Run unit tests                                   | `.\scripts\test.ps1`             |
|                  | Executes anolis_unit_tests.exe                   |                                  |
| **setup.ps1/sh** | Bootstrap vcpkg dependencies                     | `.\scripts\setup.ps1`            |
|                  | Run once after clone                             |                                  |

## Common Workflows

```powershell
# Full development environment (recommended)
.\scripts\dev.ps1

# Build only
.\scripts\build.ps1 --Clean

# Runtime only (Docker already running)
.\scripts\run.ps1

# Run tests
.\scripts\test.ps1
```

## Additional Scripts (tools/)

| Location               | Script                | Purpose                                   |
| ---------------------- | --------------------- | ----------------------------------------- |
| **tools/docker/**      | test-telemetry.ps1/sh | Full telemetry pipeline integration test  |
|                        |                       | Runtime → InfluxDB → Grafana validation   |
| **tools/operator-ui/** | serve.ps1/sh          | Start operator UI HTTP server (port 3000) |
|                        |                       | Alternative to `dev.ps1 -SkipInfra`       |

## Script Details

### dev.ps1/sh - Development Environment

**Full stack startup:**

```powershell
.\scripts\dev.ps1
```

**What it does:**

1. Validates build (offers to build if needed)
2. Starts Docker stack (InfluxDB:8086, Grafana:3001)
3. Starts operator UI server (port 3000)
4. Starts runtime (foreground)
5. Prints dashboard with all URLs

**Options:**

- `-SkipInfra` - Skip Docker containers
- `-NoUI` - Skip operator UI server
- `-SkipBuild` - Don't check/build if needed
- `-Config PATH` - Custom config file

**Cleanup:**

- Press Ctrl+C to stop runtime
- Prompted to stop Docker containers

---

### build.ps1/sh - Build Automation

**Clean release build:**

```powershell
.\scripts\build.ps1 --Clean
```

**Incremental build:**

```powershell
.\scripts\build.ps1
```

**Debug build:**

```powershell
.\scripts\build.ps1 --Debug
```

**What it builds:**

- anolis core library
- anolis-runtime.exe
- anolis-provider-sim (sibling repo)
- Unit tests

---

### run.ps1/sh - Runtime Launcher

**Default config:**

```powershell
.\scripts\run.ps1
```

**Custom config:**

```powershell
.\scripts\run.ps1 -Config myconfig.yaml
```

**What it does:**

- Detects runtime executable (Release/Debug/single-config)
- Validates config file exists
- Launches runtime in foreground

**Note:** Does NOT start Docker or UI - focused on runtime only.

---

### test-telemetry.ps1/sh - Integration Test

**Location:** `tools/docker/test-telemetry.ps1`

**Full pipeline validation:**

```powershell
cd tools\docker
.\test-telemetry.ps1
```

**What it tests:**

1. Starts Docker stack
2. Runs runtime for 30 seconds
3. Validates data written to InfluxDB
4. Verifies Grafana dashboards accessible
5. Cleans up

**Options:**

- `-SkipBuild` - Use existing build
- `-KeepRunning` - Don't stop Docker after test

---

### serve.ps1/sh - Operator UI Server

**Location:** `tools/operator-ui/serve.ps1`

**Start UI server:**

```powershell
cd tools\operator-ui
.\serve.ps1
```

**What it does:**

- Starts Python HTTP server on port 3000
- Serves static UI files
- Press Ctrl+C to stop

**When to use:**

- Manual UI testing without full dev stack
- Alternative to `dev.ps1 -SkipInfra`

---

## Cross-Platform Notes

- All scripts have `.ps1` (Windows PowerShell) and `.sh` (Linux/macOS Bash) versions
- `.sh` scripts require execute permissions: `chmod +x scripts/*.sh`
- Functionality is identical across platforms

## Prerequisites

| Tool                             | Required For       | Install                |
| -------------------------------- | ------------------ | ---------------------- |
| **CMake**                        | Building           | cmake.org              |
| **Visual Studio 2022** (Windows) | Building           | visualstudio.com       |
| **GCC/Clang** (Linux/macOS)      | Building           | System package manager |
| **Docker Desktop**               | Telemetry stack    | docker.com             |
| **Python 3**                     | Operator UI server | python.org             |

## Environment Setup

```powershell
# First-time setup
.\scripts\setup.ps1          # Bootstrap vcpkg
.\scripts\build.ps1 --Clean  # Initial build

# Start developing
.\scripts\dev.ps1            # Full dev environment
```

## Troubleshooting

**"Runtime not found":**

- Run `.\scripts\build.ps1`

**"Docker not found":**

- Install Docker Desktop or use `.\scripts\dev.ps1 -SkipInfra`

**"Python not found":**

- Install Python 3 or use `.\scripts\dev.ps1 -NoUI`

**Port conflicts (3000, 3001, 8080, 8086):**

- Stop conflicting services or edit port numbers in config files

## Quick Reference

```powershell
# I want to...                      # Run this:
Test Phase 14 automation UI         .\scripts\dev.ps1
Build everything                    .\scripts\build.ps1 --Clean
Just start runtime                  .\scripts\run.ps1
Run unit tests                      .\scripts\test.ps1
Test telemetry integration          cd tools\docker; .\test-telemetry.ps1
Serve operator UI manually          cd tools\operator-ui; .\serve.ps1
```
