#!/bin/bash
# Anolis Development Setup Script (Linux/macOS)
#
# Bootstraps local prerequisites and performs a preset-driven build.
#
# Usage:
#   ./scripts/setup.sh [--clean] [--preset <name>]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info() { echo -e "${GREEN}[INFO]${NC}  $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC}  $1"; }
error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

# ---- Extract vcpkg baseline ----
get_vcpkg_baseline() {
    if [[ -f "$REPO_ROOT/vcpkg-configuration.json" ]]; then
        grep -o '"baseline"[[:space:]]*:[[:space:]]*"[^"]*"' \
            "$REPO_ROOT/vcpkg-configuration.json" | tail -1 |
            sed 's/.*"\([^"]*\)".*/\1/' || true
    fi
}

VCPKG_BASELINE="$(get_vcpkg_baseline)"
if [[ -z "${VCPKG_BASELINE:-}" ]]; then
    error "Failed to extract baseline from vcpkg-configuration.json"
fi

# ---- Parse arguments ----
CLEAN=false
PRESET="dev-release"

while [[ $# -gt 0 ]]; do
    case "$1" in
    --clean)
        CLEAN=true
        shift
        ;;
    --preset)
        [[ $# -lt 2 ]] && error "--preset requires a value"
        PRESET="$2"
        shift 2
        ;;
    --preset=*)
        PRESET="${1#*=}"
        shift
        ;;
    *)
        error "Unknown option: $1"
        ;;
    esac
done

echo "=============================================="
echo "Anolis Development Setup"
echo "=============================================="
echo ""

# ---- Prerequisites ----
info "Checking prerequisites..."

command -v cmake >/dev/null || error "cmake not found"
command -v git >/dev/null || error "git not found"
command -v python3 >/dev/null || error "python3 not found"

if command -v g++ >/dev/null; then
    info "  C++ compiler: $(g++ --version | head -n1)"
elif command -v clang++ >/dev/null; then
    info "  C++ compiler: $(clang++ --version | head -n1)"
else
    error "No C++ compiler found"
fi

info "  cmake:   $(cmake --version | head -n1)"
info "  git:     $(git --version)"
info "  python3: $(python3 --version)"

echo ""

# ---- vcpkg Setup ----
if [[ -z "${VCPKG_ROOT:-}" ]]; then
    if [[ -d "$HOME/vcpkg" ]]; then
        export VCPKG_ROOT="$HOME/vcpkg"
    else
        info "Cloning vcpkg (baseline: $VCPKG_BASELINE)"
        git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
        export VCPKG_ROOT="$HOME/vcpkg"
    fi
fi

if [[ ! -f "$VCPKG_ROOT/vcpkg" ]]; then
    info "Bootstrapping vcpkg..."
    (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh)
fi

CURRENT_COMMIT="$(cd "$VCPKG_ROOT" && git rev-parse HEAD || echo unknown)"

if [[ "$CURRENT_COMMIT" != "$VCPKG_BASELINE" ]]; then
    warn "vcpkg commit mismatch"
    warn "  current:  $CURRENT_COMMIT"
    warn "  expected: $VCPKG_BASELINE"
    info "Checking out correct baseline..."
    (cd "$VCPKG_ROOT" && git fetch && git checkout "$VCPKG_BASELINE")
fi

info "  vcpkg baseline verified"

echo ""

# ---- Python dependencies ----
info "Installing Python dependencies..."
python3 -m pip install --upgrade pip >/dev/null

if [[ -f "$REPO_ROOT/requirements-lock.txt" ]]; then
    python3 -m pip install -r "$REPO_ROOT/requirements-lock.txt"
else
    warn "requirements-lock.txt not found"
fi

echo ""

# ---- Git submodules ----
info "Initializing submodules..."
cd "$REPO_ROOT"
git submodule update --init --recursive
info "  Submodules ready"

echo ""

# ---- Clean ----
if [[ "$CLEAN" == true ]]; then
    info "Cleaning build directory root..."
    rm -rf "$REPO_ROOT/build"
fi

echo ""

# ---- Delegate build to build.sh ----
info "Building project (preset: $PRESET)..."
"$SCRIPT_DIR/build.sh" --preset "$PRESET"

echo ""
echo "=============================================="
echo "Setup Complete"
echo "=============================================="
echo ""
echo "Next steps:"
echo "  Run tests:     ./scripts/test.sh --preset $PRESET"
echo "  Run runtime:   ./scripts/run.sh --preset $PRESET"
echo ""
