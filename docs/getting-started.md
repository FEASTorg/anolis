# Getting Started

## Prerequisites

- **Windows**: Visual Studio 2022 with C++ desktop development
- **Linux**: GCC 11+, CMake 3.20+
- **vcpkg**: For dependency management
- **Python 3**: For running tests
- **Git**: For version control

## Quick Start (Recommended)

Use the setup scripts to bootstrap your environment:

```bash
# Clone repositories
git clone https://github.com/FEASTorg/anolis.git
git clone https://github.com/FEASTorg/anolis-provider-sim.git
cd anolis

# Run setup (installs dependencies, builds both repos)
./scripts/setup.sh      # Linux/macOS
.\scripts\setup.ps1     # Windows

# Run tests
./scripts/test.sh       # Linux/macOS
.\scripts\test.ps1      # Windows

# Run validation scenarios
python scripts/run_scenarios.py

# Start the runtime
./scripts/run.sh        # Linux/macOS
.\scripts\run.ps1       # Windows
```

## Manual Build

If you prefer manual setup:

```bash
# Set VCPKG_ROOT to your vcpkg installation
export VCPKG_ROOT=~/vcpkg  # or wherever vcpkg is installed

# Build provider-sim
cd anolis-provider-sim
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Build anolis
cd ../anolis
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Run the Runtime

```bash
cd build/core/Release
./anolis-runtime.exe --config=anolis-runtime.yaml  # Windows
./anolis-runtime --config=anolis-runtime.yaml      # Linux
```

**Expected Output**:

- Provider spawns
- Hello handshake
- Devices discovered
- State cache polling starts
- HTTP API available at `http://127.0.0.1:8080`

## Quick HTTP API Test

```bash
# List devices
curl -s http://127.0.0.1:8080/v0/devices | jq

# Get device state
curl -s http://127.0.0.1:8080/v0/state/sim0/motorctl0 | jq
```

See [http-api.md](http-api.md) for full API reference.

## Operator UI

A minimal dev UI is included at `tools/operator-ui/`:

```bash
python -m http.server 3000 -d tools/operator-ui
# Open http://localhost:3000
```

## Current Limitations (v0)

- Single provider only
- No authentication
- Operator UI is minimal (developer-focused)
- Automation is policy-only (not safety-rated)

## Automation Quickstart

Enable automation and parameters in your runtime config:

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
```

Update a parameter at runtime:

```bash
curl -s -X POST http://127.0.0.1:8080/v0/parameters \
  -H "Content-Type: application/json" \
  -d '{"name": "temp_setpoint", "value": 30.0}' | jq
```

Switch to AUTO mode to run the BT:

```bash
curl -s -X POST http://127.0.0.1:8080/v0/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "AUTO"}' | jq
```

The demo behavior tree reads `temp_setpoint` via:

```xml
<GetParameter param="temp_setpoint" value="{target_temp}"/>
```

## Next Steps

- **Validation scenarios**: See [../scenarios/README.md](../scenarios/README.md) for comprehensive system validation
- **For users**: See [http-api.md](http-api.md) for API usage
- **For developers**: See [providers.md](providers.md) to create a provider
- **For contributors**: Check `working/anolis_master_plan.md` for roadmap
