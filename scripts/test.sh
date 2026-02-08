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

echo "[INFO] Running C++ unit tests..."

# First, check how many tests exist
TEST_COUNT=$(ctest --test-dir "$REPO_ROOT/build" -N -C "$CONFIG" 2>/dev/null | grep "Total Tests:" | awk '{print $3}')

if [ -z "$TEST_COUNT" ] || [ "$TEST_COUNT" = "0" ]; then
    echo "[ERROR] No unit tests found. Build may have failed or tests not configured." >&2
    exit 2
fi

echo "[INFO] Found $TEST_COUNT unit tests"

# Run all tests
ctest --test-dir "$REPO_ROOT/build" --output-on-failure -C "$CONFIG" ${VERBOSE:+-VV}
TEST_RESULT=$?

if [ $TEST_RESULT -ne 0 ]; then
    echo "[ERROR] Unit tests failed" >&2
    exit $TEST_RESULT
fi

echo "[INFO] Unit tests passed"
echo ""

python3 "tests/integration/test_all.py" $VERBOSE
exit $?
