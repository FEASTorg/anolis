#!/bin/bash
# Anolis Build Script (Linux/macOS)
#
# Usage:
#   ./scripts/build.sh [--clean] [--debug]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
PROVIDER_SIM_DIR="$(dirname "$REPO_ROOT")/anolis-provider-sim"

BUILD_TYPE="Release"
CLEAN=false

for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN=true
            ;;
        --debug)
            BUILD_TYPE="Debug"
            ;;
    esac
done

echo "[INFO] Build type: $BUILD_TYPE"

# Check vcpkg
if [ -z "$VCPKG_ROOT" ]; then
    if [ -d "$HOME/vcpkg" ]; then
        export VCPKG_ROOT="$HOME/vcpkg"
    else
        echo "[ERROR] VCPKG_ROOT not set. Run setup.sh first."
        exit 1
    fi
fi

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo "[INFO] Cleaning build directories..."
    rm -rf "$REPO_ROOT/build"
    rm -rf "$PROVIDER_SIM_DIR/build"
fi

# Build provider-sim
if [ -d "$PROVIDER_SIM_DIR" ]; then
    echo "[INFO] Building anolis-provider-sim..."
    cd "$PROVIDER_SIM_DIR"
    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    cmake --build build --config "$BUILD_TYPE" --parallel
fi

# Build anolis
echo "[INFO] Building anolis..."
cd "$REPO_ROOT"
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config "$BUILD_TYPE" --parallel

echo "[INFO] Build complete"
