#!/usr/bin/env bash
# Anolis Runtime Start Script (Linux/macOS)
#
# Usage:
#   ./scripts/run.sh [--config PATH] [--build-dir DIR]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

CONFIG_PATH="$REPO_ROOT/anolis-runtime.yaml"

# Auto-detect build directory (prefer build-tsan if TSAN build exists)
if [[ -f "$REPO_ROOT/build-tsan/CMakeCache.txt" ]] &&
	grep -q "ENABLE_TSAN:BOOL=ON" "$REPO_ROOT/build-tsan/CMakeCache.txt" 2>/dev/null; then
	BUILD_DIR="$REPO_ROOT/build-tsan"
else
	BUILD_DIR="$REPO_ROOT/build"
fi

# ------------------------------------------------------------
# Argument Parsing
# ------------------------------------------------------------
while [[ $# -gt 0 ]]; do
	case "$1" in
	--config)
		shift
		CONFIG_PATH="$1"
		;;
	--config=*)
		CONFIG_PATH="${1#*=}"
		;;
	--build-dir)
		shift
		BUILD_DIR="$1"
		;;
	--build-dir=*)
		BUILD_DIR="${1#*=}"
		;;
	*)
		echo "[ERROR] Unknown argument: $1"
		exit 2
		;;
	esac
	shift
done

# ------------------------------------------------------------
# Validation
# ------------------------------------------------------------
if [[ ! -f "$CONFIG_PATH" ]]; then
	echo "[ERROR] Config file not found: $CONFIG_PATH"
	exit 1
fi

if [[ ! -d "$BUILD_DIR" ]]; then
	echo "[ERROR] Build directory not found: $BUILD_DIR"
	echo "Run: ./scripts/build.sh"
	exit 1
fi

# ------------------------------------------------------------
# Detect Runtime Binary
# ------------------------------------------------------------
RUNTIME="$BUILD_DIR/core/anolis-runtime"

if [[ ! -x "$RUNTIME" ]]; then
	echo "[ERROR] Runtime executable not found at:"
	echo "  $RUNTIME"
	echo "Run: ./scripts/build.sh"
	exit 1
fi

# ------------------------------------------------------------
# Detect TSAN Build and Set Library Path
# ------------------------------------------------------------
if grep -q "ENABLE_TSAN:BOOL=ON" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
	echo "[INFO] ThreadSanitizer build detected"

	# Extract triplet dynamically from CMakeCache.txt
	VCPKG_TRIPLET=$(grep "VCPKG_TARGET_TRIPLET:STRING=" "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2 || true)

	if [[ -z "${VCPKG_TRIPLET:-}" ]]; then
		echo "[WARN] Could not determine VCPKG_TARGET_TRIPLET, defaulting to x64-linux"
		VCPKG_TRIPLET="x64-linux"
	fi

	export LD_LIBRARY_PATH="$BUILD_DIR/vcpkg_installed/$VCPKG_TRIPLET/lib:${LD_LIBRARY_PATH:-}"
	export TSAN_OPTIONS="second_deadlock_stack=1 detect_deadlocks=1 history_size=7"

	echo "[INFO] Using VCPKG triplet: $VCPKG_TRIPLET"
	echo "[INFO] LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
fi

# ------------------------------------------------------------
# Launch
# ------------------------------------------------------------
echo "[INFO] Starting Anolis Runtime..."
echo "[INFO] Build directory: $BUILD_DIR"
echo "[INFO] Executable:      $RUNTIME"
echo "[INFO] Config:          $CONFIG_PATH"
echo ""

exec "$RUNTIME" --config="$CONFIG_PATH"
