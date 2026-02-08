#!/bin/bash
# Anolis Test Script (Linux/macOS)
#
# Usage:
#   ./scripts/test.sh [--verbose] [--tsan] [--build-dir PATH]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$REPO_ROOT/build"
PROVIDER_BUILD_DIR="build"
PROVIDER_DIR="$(dirname "$REPO_ROOT")/anolis-provider-sim"

VERBOSE_FLAG=""

while [[ $# -gt 0 ]]; do
	case "$1" in
	--verbose | -v)
		VERBOSE_FLAG="-VV"
		shift
		;;
	--tsan)
		BUILD_DIR="$REPO_ROOT/build-tsan"
		PROVIDER_BUILD_DIR="build-tsan"
		echo "[INFO] Using TSan build directories"
		shift
		;;
	--build-dir)
		BUILD_DIR="$2"
		shift 2
		;;
	*)
		echo "[ERROR] Unknown argument: $1"
		echo "Usage: ./scripts/test.sh [--verbose] [--tsan] [--build-dir PATH]"
		exit 1
		;;
	esac
done

echo "[INFO] Running Anolis test suite..."
cd "$REPO_ROOT"

# ------------------------------------------------------------------------------
# Validate build directory
# ------------------------------------------------------------------------------

if [[ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]]; then
	echo "[ERROR] Build directory missing or not configured: $BUILD_DIR"
	echo "        Run ./scripts/build.sh first."
	exit 2
fi

# ------------------------------------------------------------------------------
# Detect TSAN
# ------------------------------------------------------------------------------

TSAN_ENABLED=false
if grep -q "ENABLE_TSAN:BOOL=ON" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
	TSAN_ENABLED=true
	echo "[INFO] ThreadSanitizer build detected"

	# Extract triplet dynamically
	VCPKG_TRIPLET=$(grep "VCPKG_TARGET_TRIPLET:STRING=" "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2 || true)

	if [[ -z "${VCPKG_TRIPLET:-}" ]]; then
		echo "[ERROR] Could not determine VCPKG_TARGET_TRIPLET from CMakeCache.txt"
		exit 3
	fi

	VCPKG_LIB="$BUILD_DIR/vcpkg_installed/$VCPKG_TRIPLET/lib"
	VCPKG_DEBUG_LIB="$BUILD_DIR/vcpkg_installed/$VCPKG_TRIPLET/debug/lib"

	export LD_LIBRARY_PATH="$VCPKG_DEBUG_LIB:$VCPKG_LIB:${LD_LIBRARY_PATH:-}"

	echo "[INFO] Using VCPKG triplet: $VCPKG_TRIPLET"
	echo "[INFO] LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

	export TSAN_OPTIONS="second_deadlock_stack=1 detect_deadlocks=1 history_size=7 log_path=$REPO_ROOT/tsan-report"
	echo "[INFO] TSAN_OPTIONS=$TSAN_OPTIONS"
	echo "[INFO] Race reports will be written to: $REPO_ROOT/tsan-report.*"

	# Ensure provider built with TSAN as well
	if [[ -f "$PROVIDER_DIR/$PROVIDER_BUILD_DIR/CMakeCache.txt" ]]; then
		if ! grep -q "ENABLE_TSAN:BOOL=ON" "$PROVIDER_DIR/$PROVIDER_BUILD_DIR/CMakeCache.txt"; then
			echo "[ERROR] Provider built without TSAN while runtime uses TSAN."
			echo "        Rebuild provider with --tsan to avoid mixed instrumentation."
			exit 4
		fi
	fi
fi

# ------------------------------------------------------------------------------
# Run C++ Unit Tests
# ------------------------------------------------------------------------------

echo "[INFO] Discovering unit tests..."

# Change to build directory for ctest
cd "$BUILD_DIR"

echo "[INFO] Running: ctest -N (test discovery)"

CTEST_TMP="$(mktemp)"
trap 'rm -f "$CTEST_TMP"' EXIT

if ! timeout 30s ctest -N >"$CTEST_TMP" 2>&1; then
	echo "[ERROR] Test discovery failed or timed out"
	cat "$CTEST_TMP"
	exit 2
fi

TEST_COUNT=$(grep -E "Total Tests: *[0-9]+" "$CTEST_TMP" | awk '{print $3}' || true)

if [[ -z "${TEST_COUNT:-}" || "$TEST_COUNT" == "0" ]]; then
	echo "[ERROR] No unit tests found."
	echo "        Ensure BUILD_TESTING=ON during configuration."
	cat "$CTEST_TMP"
	exit 2
fi

echo "[INFO] Found $TEST_COUNT unit tests"
echo "[INFO] Running unit tests..."

timeout 30m ctest --output-on-failure $VERBOSE_FLAG

cd "$REPO_ROOT"

echo "[INFO] Unit tests passed"

# ------------------------------------------------------------------------------
# Report TSAN Findings (Non-fatal preview)
# ------------------------------------------------------------------------------

if [[ "$TSAN_ENABLED" == true ]] && ls "$REPO_ROOT"/tsan-report.* >/dev/null 2>&1; then
	RACE_COUNT=$(ls "$REPO_ROOT"/tsan-report.* | wc -l)
	echo "[WARN] ThreadSanitizer detected $RACE_COUNT issue(s)"
	echo "[WARN] First report preview:"
	head -50 "$REPO_ROOT"/tsan-report.* | head -50 || true
fi

echo ""

# ------------------------------------------------------------------------------
# Integration Tests
# ------------------------------------------------------------------------------

INTEGRATION_SCRIPT="$REPO_ROOT/tests/integration/test_all.py"
if [[ ! -f "$INTEGRATION_SCRIPT" ]]; then
	echo "[WARN] Integration test script not found: $INTEGRATION_SCRIPT"
	echo "[WARN] Skipping integration tests"
else
	echo "[INFO] Running integration tests..."
	if ! python3 "$INTEGRATION_SCRIPT"; then
		echo "[ERROR] Integration tests failed"
		exit 5
	fi
	echo "[INFO] Integration tests passed"
fi

# ------------------------------------------------------------------------------
# Validation Scenarios
# ------------------------------------------------------------------------------

SCENARIO_SCRIPT="$REPO_ROOT/tests/scenarios/run_scenarios.py"
if [[ ! -f "$SCENARIO_SCRIPT" ]]; then
	echo "[WARN] Validation scenario script not found: $SCENARIO_SCRIPT"
	echo "[WARN] Skipping validation scenarios"
else
	echo "[INFO] Running validation scenarios..."
	if ! python3 "$SCENARIO_SCRIPT"; then
		echo "[ERROR] Validation scenarios failed"
		exit 6
	fi
fi

echo "[INFO] All tests passed"
exit 0
