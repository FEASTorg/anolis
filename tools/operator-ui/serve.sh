#!/usr/bin/env bash
# Anolis Operator UI - Development Server
#
# Quick helper to serve the operator UI on http://localhost:3000
# Requires Python 3 installed
#
# Usage:
#   cd tools/operator-ui
#   ./serve.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "\033[0;36mStarting Operator UI server...\033[0m"
echo -e "\033[0;32mURL: http://localhost:3000\033[0m"
echo ""
echo -e "\033[1;33mPress Ctrl+C to stop\033[0m"
echo ""

cd "$SCRIPT_DIR"

# Try python3 or python
PYTHON_CMD=""
if command -v python3 &> /dev/null; then
    PYTHON_CMD="python3"
elif command -v python &> /dev/null; then
    PYTHON_CMD="python"
fi

if [ -z "$PYTHON_CMD" ]; then
    echo -e "\033[0;31mERROR: Python not found\033[0m"
    echo -e "\033[1;33mInstall Python 3 to use this server\033[0m"
    exit 1
fi

$PYTHON_CMD -m http.server 3000
