#!/usr/bin/env bash
# Anolis setup wrapper.
# Installs prerequisites, validates local environment, then delegates to build wrapper.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

PRESET="dev-release"
CLEAN=false

usage() {
    sed -n '1,12p' "$0"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --preset)
        [[ $# -lt 2 ]] && { echo "[ERROR] --preset requires a value"; exit 2; }
        PRESET="$2"
        shift 2
        ;;
    --preset=*)
        PRESET="${1#*=}"
        shift
        ;;
    --clean)
        CLEAN=true
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "[ERROR] Unknown option: $1"
        usage
        exit 2
        ;;
    esac
done

command -v cmake >/dev/null || { echo "[ERROR] cmake not found"; exit 1; }
command -v git >/dev/null || { echo "[ERROR] git not found"; exit 1; }
command -v python3 >/dev/null || { echo "[ERROR] python3 not found"; exit 1; }

if [[ -z "${VCPKG_ROOT:-}" ]]; then
    echo "[ERROR] VCPKG_ROOT is not set. Run setup in an environment where vcpkg is installed." >&2
    exit 1
fi
if [[ ! -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
    echo "[ERROR] vcpkg toolchain not found under VCPKG_ROOT: $VCPKG_ROOT" >&2
    exit 1
fi

if [[ -f "$REPO_ROOT/requirements-lock.txt" ]]; then
    python3 -m pip install --upgrade pip
    python3 -m pip install -r "$REPO_ROOT/requirements-lock.txt"
fi

(
    cd "$REPO_ROOT"
    git submodule update --init --recursive
)

if [[ "$CLEAN" == true ]]; then
    rm -rf "$REPO_ROOT/build"
fi

bash "$SCRIPT_DIR/build.sh" --preset "$PRESET"
