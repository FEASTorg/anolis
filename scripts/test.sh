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

# Detect if TSAN is enabled in build
TSAN_ENABLED=false
if grep -q "fsanitize=thread" "$REPO_ROOT/build/CMakeCache.txt" 2>/dev/null || \
   grep -q "ENABLE_TSAN:BOOL=ON" "$REPO_ROOT/build/CMakeCache.txt" 2>/dev/null; then
    TSAN_ENABLED=true
    echo "[INFO] ThreadSanitizer build detected"
    
    # Set LD_LIBRARY_PATH to use TSAN-instrumented libraries
    export LD_LIBRARY_PATH="$REPO_ROOT/build/vcpkg_installed/x64-linux-tsan/lib:${LD_LIBRARY_PATH:-}"
    echo "[INFO] LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    
    # Configure TSAN to capture ALL races, not halt on first error
    # This lets us see the actual race before any crash
    export TSAN_OPTIONS="second_deadlock_stack=1 detect_deadlocks=1 history_size=7 log_path=$REPO_ROOT/tsan-report"
    echo "[INFO] TSAN_OPTIONS=$TSAN_OPTIONS"
    echo "[INFO] Race reports will be written to: $REPO_ROOT/tsan-report.*"
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

# Check for TSAN race reports from unit tests
if [ "$TSAN_ENABLED" = true ] && ls "$REPO_ROOT"/tsan-report.* 1>/dev/null 2>&1; then
    echo "[WARN] ThreadSanitizer detected races in unit tests:"
    for report in "$REPO_ROOT"/tsan-report.*; do
        echo "  - $(basename "$report")"
    done
    echo "[INFO] Review reports above. Continuing to integration tests..."
fi

echo ""

# Run integration tests
echo "[INFO] Running integration tests..."
python3 "tests/integration/test_all.py" $VERBOSE
INTEGRATION_RESULT=$?

if [ $INTEGRATION_RESULT -ne 0 ]; then
    echo "[ERROR] Integration tests failed" >&2
    exit $INTEGRATION_RESULT
fi

echo "[INFO] Integration tests passed"

# Check for TSAN race reports from integration tests
if [ "$TSAN_ENABLED" = true ] && ls "$REPO_ROOT"/tsan-report.* 1>/dev/null 2>&1; then
    echo "[WARN] ThreadSanitizer detected races during testing:"
    RACE_COUNT=$(ls "$REPO_ROOT"/tsan-report.* 2>/dev/null | wc -l)
    echo "[WARN] Total race reports: $RACE_COUNT"
    echo "[WARN] First report preview:"
    head -50 "$REPO_ROOT"/tsan-report.* 2>/dev/null | head -50 || true
    echo ""
    echo "[WARN] Full reports available in: $REPO_ROOT/tsan-report.*"
fi

echo ""

# Run validation scenarios
echo "[INFO] Running validation scenarios..."
python3 "tests/scenarios/run_scenarios.py" $VERBOSE
SCENARIO_RESULT=$?

if [ $SCENARIO_RESULT -ne 0 ]; then
    echo "[ERROR] Validation scenarios failed" >&2
    exit $SCENARIO_RESULT
fi

echo "[INFO] All tests passed"
exit 0
