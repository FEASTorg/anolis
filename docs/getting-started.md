# Getting Started

## Prerequisites

- **Windows**: Visual Studio 2022 with C++ desktop development
- **Linux**: GCC 11+, CMake 3.20+
- **vcpkg**: dependency management
- **Python 3**: test tooling
- **Git**: source control

## Quick Start (Preset-First)

```bash
# Clone
git clone https://github.com/FEASTorg/anolis.git
cd anolis

# Pull required protocol submodule
git submodule update --init --recursive

# Setup + build
bash ./scripts/setup.sh --preset dev-release      # Linux/macOS
# .\scripts\setup.ps1 -Preset dev-windows-release # Windows

# Test
bash ./scripts/test.sh --preset dev-release       # Linux/macOS
# .\scripts\test.ps1 -Preset dev-windows-release  # Windows

# Run runtime
bash ./scripts/run.sh --preset dev-release        # Linux/macOS
# .\scripts\run.ps1 -Preset dev-windows-release   # Windows
```

Use presets directly when needed:

```bash
cmake --list-presets
ctest --list-presets
```

## Runtime API Smoke Check

```bash
# List devices
curl -s http://127.0.0.1:8080/v0/devices | jq

# Read state
curl -s http://127.0.0.1:8080/v0/state/sim0/motorctl0 | jq
```

See [http-api.md](http-api.md) for full API details.

## Operator UI

```bash
python -m http.server 3000 -d tools/operator-ui
# Open http://localhost:3000
```

## Automation Quickstart

Enable automation in runtime config:

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

Switch to AUTO mode:

```bash
curl -s -X POST http://127.0.0.1:8080/v0/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "AUTO"}' | jq
```

## Next Steps

- For provider integration and safety contracts: [providers.md](providers.md)
- For runtime config details: [configuration.md](configuration.md)
- For contributor workflow: [../CONTRIBUTING.md](../CONTRIBUTING.md)
