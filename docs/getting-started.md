# Getting Started

## Prerequisites

- **Windows**: Visual Studio 2022 with C++ desktop development
- **Linux**: gcc 11+, cmake 3.20+
- **vcpkg**: For dependency management
- **Git**: For cloning repos

## Clone Repositories

```bash
# Runtime
git clone https://github.com/FEASTorg/anolis.git
cd anolis

# Example provider (for testing)
git clone https://github.com/FEASTorg/anolis-provider-sim.git
```

## Build anolis-provider-sim

```bash
cd anolis-provider-sim
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Build Anolis Core

```bash
cd anolis
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

**Note**: Set `CMAKE_TOOLCHAIN_FILE` to your vcpkg installation path.

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
- No behavior trees
- No streaming (polling only)

## Next Steps

- **For users**: See [http-api.md](http-api.md) for API usage
- **For developers**: See [providers.md](providers.md) to create a provider
- **For contributors**: Check `working/anolis_master_plan.md` for roadmap
