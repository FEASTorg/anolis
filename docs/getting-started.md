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

## Run Phase 3A Test

```bash
cd build/core/Release
./anolis-runtime.exe  # Windows
./anolis-runtime      # Linux
```

**Expected Output**:

- Provider spawns
- Hello handshake
- 2 devices discovered (tempctl0, motorctl0)
- State cache polls 10 signals
- Relay control tests (OFF→ON→OFF)
- Clean shutdown

## Current Limitations (v0)

- No config file support yet (Phase 3.5)
- Provider path hardcoded in main.cpp
- Single provider only
- No HTTP API
- No Behavior Trees

## Next Steps

- **For users**: Wait for Phase 3B (config + CLI)
- **For developers**: See [providers.md](providers.md) to create a provider
- **For contributors**: Check `working/phase_3_plan.md` for roadmap
