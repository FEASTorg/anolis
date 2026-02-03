#!/bin/bash
# Anolis Development Setup Script (Linux/macOS)
#
# This script bootstraps a development environment:
# - Checks prerequisites
# - Sets up vcpkg
# - Builds anolis and anolis-provider-sim
#
# Usage:
#   ./scripts/setup.sh [--clean]
#
# Options:
#   --clean    Remove build directories before building

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
PROVIDER_SIM_DIR="$(dirname "$REPO_ROOT")/anolis-provider-sim"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

# Parse arguments
CLEAN=false
for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN=true
            shift
            ;;
        *)
            ;;
    esac
done

echo "=============================================="
echo "Anolis Development Setup"
echo "=============================================="
echo ""

# Check prerequisites
info "Checking prerequisites..."

# Check cmake
if ! command -v cmake &> /dev/null; then
    error "cmake not found. Install with: sudo apt install cmake (Ubuntu) or brew install cmake (macOS)"
fi
CMAKE_VERSION=$(cmake --version | head -n1 | awk '{print $3}')
info "  cmake: $CMAKE_VERSION"

# Check C++ compiler
if command -v g++ &> /dev/null; then
    CXX_VERSION=$(g++ --version | head -n1)
    info "  g++: $CXX_VERSION"
elif command -v clang++ &> /dev/null; then
    CXX_VERSION=$(clang++ --version | head -n1)
    info "  clang++: $CXX_VERSION"
else
    error "No C++ compiler found. Install with: sudo apt install g++ (Ubuntu) or xcode-select --install (macOS)"
fi

# Check python3
if ! command -v python3 &> /dev/null; then
    error "python3 not found. Install with: sudo apt install python3 (Ubuntu) or brew install python3 (macOS)"
fi
PYTHON_VERSION=$(python3 --version)
info "  python3: $PYTHON_VERSION"

# Check git
if ! command -v git &> /dev/null; then
    error "git not found. Install with: sudo apt install git"
fi
GIT_VERSION=$(git --version)
info "  git: $GIT_VERSION"

# Check/setup vcpkg
if [ -z "$VCPKG_ROOT" ]; then
    # Check common locations
    if [ -d "$HOME/vcpkg" ]; then
        export VCPKG_ROOT="$HOME/vcpkg"
    elif [ -d "/opt/vcpkg" ]; then
        export VCPKG_ROOT="/opt/vcpkg"
    else
        warn "VCPKG_ROOT not set and vcpkg not found in common locations."
        info "Installing vcpkg to ~/vcpkg..."
        git clone https://github.com/Microsoft/vcpkg.git "$HOME/vcpkg"
        "$HOME/vcpkg/bootstrap-vcpkg.sh"
        export VCPKG_ROOT="$HOME/vcpkg"
    fi
fi

if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
    error "vcpkg executable not found at $VCPKG_ROOT/vcpkg"
fi
info "  vcpkg: $VCPKG_ROOT"

# Check pip packages
info "Checking Python packages..."
python3 -m pip install --quiet --upgrade pip
python3 -m pip install --quiet requests
info "  requests: installed"

echo ""

# Initialize submodules
info "Initializing git submodules..."
cd "$REPO_ROOT"
git submodule update --init --recursive
info "  Submodules initialized"

echo ""

# Check for provider-sim repo
if [ ! -d "$PROVIDER_SIM_DIR" ]; then
    warn "anolis-provider-sim not found at $PROVIDER_SIM_DIR"
    info "Cloning anolis-provider-sim..."
    git clone https://github.com/FEASTorg/anolis-provider-sim.git "$PROVIDER_SIM_DIR"
fi

# Clean if requested
if [ "$CLEAN" = true ]; then
    info "Cleaning build directories..."
    rm -rf "$REPO_ROOT/build"
    rm -rf "$PROVIDER_SIM_DIR/build"
    info "  Cleaned"
fi

echo ""

# Build anolis-provider-sim
info "Building anolis-provider-sim..."
cd "$PROVIDER_SIM_DIR"

if [ ! -d "build" ]; then
    info "  Configuring..."
    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
fi

info "  Compiling..."
cmake --build build --config Release --parallel

if [ -f "build/anolis-provider-sim" ] || [ -f "build/Release/anolis-provider-sim" ]; then
    info "  anolis-provider-sim built successfully"
else
    error "Failed to build anolis-provider-sim"
fi

echo ""

# Build anolis
info "Building anolis..."
cd "$REPO_ROOT"

if [ ! -d "build" ]; then
    info "  Configuring..."
    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
fi

info "  Compiling..."
cmake --build build --config Release --parallel

if [ -f "build/core/anolis-runtime" ] || [ -f "build/core/Release/anolis-runtime" ]; then
    info "  anolis-runtime built successfully"
else
    error "Failed to build anolis-runtime"
fi

echo ""
echo "=============================================="
echo "Setup Complete!"
echo "=============================================="
echo ""
echo "Next steps:"
echo "  1. Run tests:     ./scripts/test.sh"
echo "  2. Start runtime: ./scripts/run.sh"
echo ""
