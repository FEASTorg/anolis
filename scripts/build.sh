#!/bin/bash
# Anolis Build Script (Linux/macOS)
#
# Usage:
#   ./scripts/build.sh [options]
#
# Options:
#   --clean                 Remove build directories before configuring
#   --debug                 Use Debug build type (default: Release)
#   --release               Use Release build type
#   --no-tests              Configure with BUILD_TESTING=OFF
#   --clang-tidy            Enable clang-tidy (default)
#   --no-clang-tidy         Disable clang-tidy
#   --tsan                  Enable ThreadSanitizer (requires TSAN triplet + ENABLE_TSAN=ON)
#   --generator <name>      CMake generator (default: Ninja if available, else platform default)
#   -j, --jobs <N>          Parallel build jobs (default: auto)
#   -h, --help              Show help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
PROVIDER_SIM_DIR="$(dirname "$REPO_ROOT")/anolis-provider-sim"

BUILD_TYPE="Release"
CLEAN=false
BUILD_TESTS=true
CLANG_TIDY=true
TSAN=false
GENERATOR=""
JOBS=""

usage() {
	sed -n '1,80p' "$0"
}

# Parse args
while [[ $# -gt 0 ]]; do
	case "$1" in
	--clean)
		CLEAN=true
		shift
		;;
	--debug)
		BUILD_TYPE="Debug"
		shift
		;;
	--release)
		BUILD_TYPE="Release"
		shift
		;;
	--no-tests)
		BUILD_TESTS=false
		shift
		;;
	--clang-tidy)
		CLANG_TIDY=true
		shift
		;;
	--no-clang-tidy)
		CLANG_TIDY=false
		shift
		;;
	--tsan)
		TSAN=true
		shift
		;;
	--generator)
		[[ $# -lt 2 ]] && {
			echo "[ERROR] --generator requires a value"
			exit 2
		}
		GENERATOR="$2"
		shift 2
		;;
	-j | --jobs)
		[[ $# -lt 2 ]] && {
			echo "[ERROR] $1 requires a value"
			exit 2
		}
		JOBS="$2"
		shift 2
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

echo "[INFO] Build type: $BUILD_TYPE"
echo "[INFO] Build tests: $BUILD_TESTS"
echo "[INFO] clang-tidy: $CLANG_TIDY"
echo "[INFO] ThreadSanitizer: $TSAN"

# Prefer a separate build dir for TSAN to avoid mixing CMake caches/artifacts
BUILD_DIR="$REPO_ROOT/build"
PROVIDER_BUILD_DIR="$PROVIDER_SIM_DIR/build"
if [[ "$TSAN" == true ]]; then
	BUILD_DIR="$REPO_ROOT/build-tsan"
	PROVIDER_BUILD_DIR="$PROVIDER_SIM_DIR/build-tsan"
fi

# Choose generator
if [[ -z "$GENERATOR" ]]; then
	if command -v ninja >/dev/null 2>&1; then
		GENERATOR="Ninja"
	else
		GENERATOR="" # let CMake decide
	fi
fi
GENERATOR_ARGS=()
if [[ -n "$GENERATOR" ]]; then
	GENERATOR_ARGS=(-G "$GENERATOR")
fi

# Parallel jobs
BUILD_ARGS=(--parallel)
if [[ -n "$JOBS" ]]; then
	BUILD_ARGS=(--parallel "$JOBS")
fi

# vcpkg
if [[ -z "${VCPKG_ROOT:-}" ]]; then
	if [[ -d "$HOME/vcpkg" ]]; then
		export VCPKG_ROOT="$HOME/vcpkg"
	else
		echo "[ERROR] VCPKG_ROOT not set and $HOME/vcpkg not found. Run setup.sh first."
		exit 1
	fi
fi

if [[ ! -d "$VCPKG_ROOT/scripts/buildsystems" ]]; then
	echo "[ERROR] VCPKG_ROOT doesn't look valid: $VCPKG_ROOT"
	exit 1
fi

# Triplet selection
VCPKG_TRIPLET="x64-linux"
if [[ "$TSAN" == true ]]; then
	VCPKG_TRIPLET="x64-linux-tsan"
	echo "[INFO] Using TSAN triplet: $VCPKG_TRIPLET (deps rebuilt with -fsanitize=thread)"
fi

OVERLAY_TRIPLETS="$REPO_ROOT/triplets"
OVERLAY_ARGS=()
if [[ -d "$OVERLAY_TRIPLETS" ]]; then
	OVERLAY_ARGS=(-DVCPKG_OVERLAY_TRIPLETS="$OVERLAY_TRIPLETS")
fi

TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
COMMON_CONFIG_ARGS=(
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE"
	-DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET"
	"${OVERLAY_ARGS[@]}"
	-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
)

ANOLIS_CONFIG_ARGS=(
	"${COMMON_CONFIG_ARGS[@]}"
	-DBUILD_TESTING="$([[ "$BUILD_TESTS" == true ]] && echo "ON" || echo "OFF")"
	-DENABLE_CLANG_TIDY="$([[ "$CLANG_TIDY" == true ]] && echo "ON" || echo "OFF")"
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [[ "$TSAN" == true ]]; then
	ANOLIS_CONFIG_ARGS+=(-DENABLE_TSAN=ON)
fi

PROVIDER_CONFIG_ARGS=(
	"${COMMON_CONFIG_ARGS[@]}"
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

# IMPORTANT: provider-sim must also be TSAN-instrumented when TSAN is enabled,
# otherwise you'll get mixed-instrumentation crashes / TSAN deadly signals.
if [[ "$TSAN" == true ]]; then
	PROVIDER_CONFIG_ARGS+=(-DENABLE_TSAN=ON)
fi

# Clean if requested
if [[ "$CLEAN" == true ]]; then
	echo "[INFO] Cleaning build directories..."
	rm -rf "$BUILD_DIR"
	if [[ -d "$PROVIDER_SIM_DIR" ]]; then
		rm -rf "$PROVIDER_BUILD_DIR"
	fi
fi

# Build provider-sim (optional)
if [[ -d "$PROVIDER_SIM_DIR" ]]; then
	echo "[INFO] Building anolis-provider-sim..."
	cmake -B "$PROVIDER_BUILD_DIR" -S "$PROVIDER_SIM_DIR" "${GENERATOR_ARGS[@]}" "${PROVIDER_CONFIG_ARGS[@]}"
	cmake --build "$PROVIDER_BUILD_DIR" "${BUILD_ARGS[@]}"
else
	echo "[INFO] anolis-provider-sim not found at: $PROVIDER_SIM_DIR (skipping)"
fi

# Build anolis
echo "[INFO] Building anolis..."
cmake -B "$BUILD_DIR" -S "$REPO_ROOT" "${GENERATOR_ARGS[@]}" "${ANOLIS_CONFIG_ARGS[@]}"
cmake --build "$BUILD_DIR" "${BUILD_ARGS[@]}"

echo "[INFO] Build complete"
echo "[INFO] Build dir: $BUILD_DIR"
if [[ -d "$PROVIDER_SIM_DIR" ]]; then
	echo "[INFO] Provider build dir: $PROVIDER_BUILD_DIR"
fi
