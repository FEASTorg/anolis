#!/bin/bash
# Anolis Runtime Start Script (Linux/macOS)
#
# Usage:
#   ./scripts/run.sh [--config PATH]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

# Default config
CONFIG_PATH="$REPO_ROOT/anolis-runtime.yaml"

# Parse args
for arg in "$@"; do
    case $arg in
        --config=*)
            CONFIG_PATH="${arg#*=}"
            ;;
    esac
done

# Find runtime executable
RUNTIME=""
if [ -f "$REPO_ROOT/build/core/Release/anolis-runtime" ]; then
    RUNTIME="$REPO_ROOT/build/core/Release/anolis-runtime"
elif [ -f "$REPO_ROOT/build/core/anolis-runtime" ]; then
    RUNTIME="$REPO_ROOT/build/core/anolis-runtime"
elif [ -f "$REPO_ROOT/build/core/Debug/anolis-runtime" ]; then
    RUNTIME="$REPO_ROOT/build/core/Debug/anolis-runtime"
else
    echo "[ERROR] anolis-runtime not found. Run build.sh first."
    exit 1
fi

echo "[INFO] Starting Anolis Runtime..."
echo "[INFO] Executable: $RUNTIME"
echo "[INFO] Config: $CONFIG_PATH"
echo ""

exec "$RUNTIME" --config="$CONFIG_PATH"
