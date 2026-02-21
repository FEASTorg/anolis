#!/usr/bin/env bash
# Anolis test wrapper (preset-first)
#
# Usage:
#   bash ./scripts/test.sh [options] [-- <extra-ctest-args>]
#
# Options:
#   --preset <name>   Test preset (default: dev-release)
#   --label <expr>    Run only tests matching label expression (-L)
#   -v, --verbose     Run ctest with -VV
#   -h, --help        Show help

set -euo pipefail

PRESET="dev-release"
LABEL_EXPR=""
VERBOSE=false
EXTRA_CTEST_ARGS=()

usage() {
    sed -n '1,20p' "$0"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --preset)
        [[ $# -lt 2 ]] && {
            echo "[ERROR] --preset requires a value"
            exit 2
        }
        PRESET="$2"
        shift 2
        ;;
    --label)
        [[ $# -lt 2 ]] && {
            echo "[ERROR] --label requires a value"
            exit 2
        }
        LABEL_EXPR="$2"
        shift 2
        ;;
    -v | --verbose)
        VERBOSE=true
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    --)
        shift
        EXTRA_CTEST_ARGS=("$@")
        break
        ;;
    *)
        echo "[ERROR] Unknown option: $1"
        usage
        exit 2
        ;;
    esac
done

CTEST_ARGS=(--preset "$PRESET")
if [[ -n "$LABEL_EXPR" ]]; then
    CTEST_ARGS+=( -L "$LABEL_EXPR" )
fi
if [[ "$VERBOSE" == true ]]; then
    CTEST_ARGS+=( -VV )
fi
if [[ ${#EXTRA_CTEST_ARGS[@]} -gt 0 ]]; then
    CTEST_ARGS+=( "${EXTRA_CTEST_ARGS[@]}" )
fi

echo "[INFO] Test preset: $PRESET"
ctest "${CTEST_ARGS[@]}"
