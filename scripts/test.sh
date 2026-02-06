#!/bin/bash
# Anolis Test Script (Linux/macOS)
#
# Usage:
#   ./scripts/test.sh [--verbose] [--config Release]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

VERBOSE=""
CONFIG="Release"

while [ $# -gt 0 ]; do
    case "$1" in
        --verbose|-v)
            VERBOSE="--verbose"
            ;;
        --config)
            shift
            CONFIG="$1"
            ;;
        --config=*)
            CONFIG="${1#*=}"
            ;;
    esac
    shift
done

echo "[INFO] Running Anolis test suite..."
cd "$REPO_ROOT"

# Run C++ unit tests via CTest (requires existing build directory)
if [ ! -f "$REPO_ROOT/build/CTestTestfile.cmake" ]; then
  echo "[ERROR] Build directory missing (expected $REPO_ROOT/build). Please configure & build before running tests." >&2
  exit 2
fi

ctest --output-on-failure -C "$CONFIG" -R anolis_unit_tests ${VERBOSE:+-VV}

python3 "tests/integration/test_all.py" $VERBOSE
exit $?
