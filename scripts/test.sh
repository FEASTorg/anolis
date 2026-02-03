#!/bin/bash
# Anolis Test Script (Linux/macOS)
#
# Usage:
#   ./scripts/test.sh [--verbose]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

VERBOSE=""
for arg in "$@"; do
    case $arg in
        --verbose|-v)
            VERBOSE="--verbose"
            ;;
    esac
done

echo "[INFO] Running Anolis test suite..."
cd "$REPO_ROOT"

python3 "$SCRIPT_DIR/test_all.py" $VERBOSE
exit $?
