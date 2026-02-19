#!/usr/bin/env bash
#
# Run clang-format over C++ sources.
# Defaults to all directories if no flags are provided.
# Usage:
#   ./scripts/format-cpp.sh
#   ./scripts/format-cpp.sh --core --tests
#

set -euo pipefail

# Resolve repo root (script lives in scripts/)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Directories (relative to repo root)
declare -A DIRS=(
  [core]="$REPO_ROOT/core"
  [tests]="$REPO_ROOT/tests"
)

# Parse flags
SELECTED=()

for arg in "$@"; do
  case "$arg" in
    --core)  SELECTED+=("${DIRS[core]}") ;;
    --tests) SELECTED+=("${DIRS[tests]}") ;;
    *) echo "Unknown option: $arg" >&2; exit 1 ;;
  esac
done

# Default: all
if [ ${#SELECTED[@]} -eq 0 ]; then
  for d in "${DIRS[@]}"; do
    SELECTED+=("$d")
  done
fi

TOTAL=0

for dir in "${SELECTED[@]}"; do
  if [ -d "$dir" ]; then
    mapfile -t FILES < <(find "$dir" -type f \( -name "*.cpp" -o -name "*.hpp" \))
    COUNT=${#FILES[@]}

    if [ "$COUNT" -gt 0 ]; then
      echo "Formatting $COUNT file(s) in $dir"
      for f in "${FILES[@]}"; do
        clang-format -i "$f"
      done
      TOTAL=$((TOTAL + COUNT))
    fi
  fi
done

echo
echo "Formatted $TOTAL file(s)."
