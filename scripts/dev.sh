#!/usr/bin/env bash
# Anolis developer convenience wrapper (non-authoritative).
# Delegates to build + test preset wrappers.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PRESET="dev-release"
CLEAN=false
SKIP_TESTS=false
BUILD_EXTRA_ARGS=()

usage() {
    sed -n '1,14p' "$0"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --preset)
        [[ $# -lt 2 ]] && { echo "[ERROR] --preset requires a value"; exit 2; }
        PRESET="$2"
        shift 2
        ;;
    --clean)
        CLEAN=true
        shift
        ;;
    --skip-tests)
        SKIP_TESTS=true
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    --)
        shift
        BUILD_EXTRA_ARGS=("$@")
        break
        ;;
    *)
        echo "[ERROR] Unknown option: $1"
        usage
        exit 2
        ;;
    esac
done

BUILD_ARGS=(--preset "$PRESET")
if [[ "$CLEAN" == true ]]; then
    BUILD_ARGS+=(--clean)
fi
if [[ ${#BUILD_EXTRA_ARGS[@]} -gt 0 ]]; then
    BUILD_ARGS+=(--)
    BUILD_ARGS+=("${BUILD_EXTRA_ARGS[@]}")
fi

bash "$SCRIPT_DIR/build.sh" "${BUILD_ARGS[@]}"

if [[ "$SKIP_TESTS" == false ]]; then
    bash "$SCRIPT_DIR/test.sh" --preset "$PRESET"
fi
