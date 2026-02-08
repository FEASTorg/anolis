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

	# Extract triplet dynamically (for verification only)
	VCPKG_TRIPLET=$(grep "VCPKG_TARGET_TRIPLET:STRING=" "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2 || true)

	if [[ -z "${VCPKG_TRIPLET:-}" ]]; then
		echo "[ERROR] Could not determine VCPKG_TARGET_TRIPLET from CMakeCache.txt"
		exit 3
	fi

	echo "[INFO] Using VCPKG triplet: $VCPKG_TRIPLET"

	# IMPORTANT: Do NOT set LD_LIBRARY_PATH here!
	# Test binaries have RPATH configured by CMake to find TSAN-instrumented libraries.
	# Setting LD_LIBRARY_PATH causes ctest itself to load TSAN libraries, which
	# triggers segfaults during fork(). Only the test executables should load TSAN.

	# Configure TSAN runtime behavior for test processes
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

cd "$BUILD_DIR"

echo "[INFO] Found build dir: $BUILD_DIR"
echo "[INFO] Discovering unit tests..."

CTEST_OUT="$(mktemp)"
trap 'rm -f "$CTEST_OUT"' EXIT

if ! ctest -N --test-dir "$BUILD_DIR" >"$CTEST_OUT" 2>&1; then
	echo "[ERROR] ctest discovery failed"
	cat "$CTEST_OUT"
	exit 2
fi

TEST_COUNT="$(grep -E "Total Tests: *[0-9]+" "$CTEST_OUT" | awk '{print $3}' || true)"
if [[ -z "${TEST_COUNT:-}" || "$TEST_COUNT" == "0" ]]; then
	echo "[ERROR] No unit tests found."
	cat "$CTEST_OUT"
	exit 2
fi

echo "[INFO] Found $TEST_COUNT unit tests"
echo "[INFO] Running unit tests..."
ctest --test-dir "$BUILD_DIR" --output-on-failure $VERBOSE_FLAG

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
	
	# Find runtime executable in the build directory
	RUNTIME_PATH=""
	for candidate in "$BUILD_DIR/core/Release/anolis-runtime" "$BUILD_DIR/core/Debug/anolis-runtime" "$BUILD_DIR/core/anolis-runtime"; do
		if [[ -f "$candidate" && -x "$candidate" ]]; then
			RUNTIME_PATH="$candidate"
			break
		fi
	done
	
	if [[ -z "$RUNTIME_PATH" ]]; then
		echo "[ERROR] Runtime executable not found in $BUILD_DIR/core/"
		echo "[ERROR] Expected: anolis-runtime"
		exit 5
	fi
	
	# Find provider executable (optional)
	PROVIDER_ARGS=()
	PROVIDER_PATH=""
	for candidate in "$PROVIDER_DIR/$PROVIDER_BUILD_DIR/Release/anolis-provider-sim" "$PROVIDER_DIR/$PROVIDER_BUILD_DIR/Debug/anolis-provider-sim" "$PROVIDER_DIR/$PROVIDER_BUILD_DIR/anolis-provider-sim"; do
		if [[ -f "$candidate" && -x "$candidate" ]]; then
			PROVIDER_PATH="$candidate"
			break
		fi
	done
	
	if [[ -n "$PROVIDER_PATH" ]]; then
		PROVIDER_ARGS=(--provider "$PROVIDER_PATH")
	fi
	
	echo "[INFO] Runtime: $RUNTIME_PATH"
	if [[ -n "$PROVIDER_PATH" ]]; then
		echo "[INFO] Provider: $PROVIDER_PATH"
	fi
	
	if ! python3 "$INTEGRATION_SCRIPT" --runtime "$RUNTIME_PATH" "${PROVIDER_ARGS[@]}"; then
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
	
	# Reuse runtime and provider paths from integration tests
	SCENARIO_ARGS=()
	if [[ -n "${RUNTIME_PATH:-}" ]]; then
		SCENARIO_ARGS+=(--runtime "$RUNTIME_PATH")
	fi
	if [[ -n "${PROVIDER_PATH:-}" ]]; then
		SCENARIO_ARGS+=(--provider "$PROVIDER_PATH")
	fi
	
	if ! python3 "$SCENARIO_SCRIPT" "${SCENARIO_ARGS[@]}"; then
		echo "[ERROR] Validation scenarios failed"
		exit 6
	fi
	echo "[INFO] Validation scenarios passed"
fi

echo "[INFO] All tests passed"
exit 0
