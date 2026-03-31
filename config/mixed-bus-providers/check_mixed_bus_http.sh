#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  check_mixed_bus_http.sh [options]

Options:
  --base-url URL              Runtime base URL (default: http://127.0.0.1:8080)
  --expect-providers CSV      Comma-separated expected provider IDs (default: bread0,ezo0)
  --min-device-count N        Minimum total device count expected (default: 2)
  --allow-unavailable         Do not fail if provider state is UNAVAILABLE
  --capture-dir DIR           Write raw endpoint JSON files and summary into DIR
  -h, --help                  Show this help

Environment fallbacks:
  ANOLIS_BASE_URL
  EXPECTED_PROVIDERS
  MIN_DEVICE_COUNT
USAGE
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: required command not found: $1" >&2
    exit 2
  fi
}

BASE_URL="${ANOLIS_BASE_URL:-http://127.0.0.1:8080}"
EXPECTED_PROVIDERS="${EXPECTED_PROVIDERS:-bread0,ezo0}"
MIN_DEVICE_COUNT="${MIN_DEVICE_COUNT:-2}"
ALLOW_UNAVAILABLE=0
CAPTURE_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --base-url)
      BASE_URL="$2"
      shift 2
      ;;
    --expect-providers)
      EXPECTED_PROVIDERS="$2"
      shift 2
      ;;
    --min-device-count)
      MIN_DEVICE_COUNT="$2"
      shift 2
      ;;
    --allow-unavailable)
      ALLOW_UNAVAILABLE=1
      shift
      ;;
    --capture-dir)
      CAPTURE_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

need_cmd curl
need_cmd jq

if ! [[ "$MIN_DEVICE_COUNT" =~ ^[0-9]+$ ]]; then
  echo "error: --min-device-count must be a non-negative integer" >&2
  exit 2
fi

if [[ -n "$CAPTURE_DIR" ]]; then
  mkdir -p "$CAPTURE_DIR"
fi

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

RUNTIME_JSON="$TMP_DIR/runtime_status.json"
HEALTH_JSON="$TMP_DIR/providers_health.json"
DEVICES_JSON="$TMP_DIR/devices.json"
STATE_JSON="$TMP_DIR/state.json"

curl -fsS "$BASE_URL/v0/runtime/status" > "$RUNTIME_JSON"
curl -fsS "$BASE_URL/v0/providers/health" > "$HEALTH_JSON"
curl -fsS "$BASE_URL/v0/devices" > "$DEVICES_JSON"
curl -fsS "$BASE_URL/v0/state" > "$STATE_JSON"

jq -e '.status.code == "OK"' "$RUNTIME_JSON" >/dev/null
jq -e '.status.code == "OK"' "$HEALTH_JSON" >/dev/null
jq -e '.status.code == "OK"' "$DEVICES_JSON" >/dev/null
jq -e '.status.code == "OK"' "$STATE_JSON" >/dev/null

TOTAL_DEVICES="$(jq -r '.device_count' "$RUNTIME_JSON")"
if ! [[ "$TOTAL_DEVICES" =~ ^[0-9]+$ ]]; then
  echo "error: runtime status did not return a numeric device_count" >&2
  exit 1
fi
if (( TOTAL_DEVICES < MIN_DEVICE_COUNT )); then
  echo "error: total device_count=$TOTAL_DEVICES is below required minimum $MIN_DEVICE_COUNT" >&2
  exit 1
fi

IFS=',' read -r -a EXPECTED_ARRAY <<< "$EXPECTED_PROVIDERS"
for provider_id in "${EXPECTED_ARRAY[@]}"; do
  provider_id="${provider_id//[[:space:]]/}"
  [[ -z "$provider_id" ]] && continue

  jq -e --arg p "$provider_id" '.providers[] | select(.provider_id == $p)' "$RUNTIME_JSON" >/dev/null || {
    echo "error: expected provider '$provider_id' missing from /v0/runtime/status" >&2
    exit 1
  }

  jq -e --arg p "$provider_id" '.providers[] | select(.provider_id == $p)' "$HEALTH_JSON" >/dev/null || {
    echo "error: expected provider '$provider_id' missing from /v0/providers/health" >&2
    exit 1
  }

  if (( ALLOW_UNAVAILABLE == 0 )); then
    state_runtime="$(jq -r --arg p "$provider_id" '.providers[] | select(.provider_id == $p) | .state' "$RUNTIME_JSON")"
    state_health="$(jq -r --arg p "$provider_id" '.providers[] | select(.provider_id == $p) | .state' "$HEALTH_JSON")"

    if [[ "$state_runtime" != "AVAILABLE" ]]; then
      echo "error: provider '$provider_id' runtime state is '$state_runtime' (expected AVAILABLE)" >&2
      exit 1
    fi
    if [[ "$state_health" != "AVAILABLE" ]]; then
      echo "error: provider '$provider_id' health state is '$state_health' (expected AVAILABLE)" >&2
      exit 1
    fi
  fi
done

echo "mixed-bus HTTP check: PASS"
echo "  base_url: $BASE_URL"
echo "  expected_providers: $EXPECTED_PROVIDERS"
echo "  total_device_count: $TOTAL_DEVICES"
echo "  providers:"
jq -r '.providers[] | "    - \(.provider_id): state=\(.state), device_count=\(.device_count)"' "$RUNTIME_JSON"

if [[ -n "$CAPTURE_DIR" ]]; then
  cp "$RUNTIME_JSON" "$CAPTURE_DIR/runtime_status.json"
  cp "$HEALTH_JSON" "$CAPTURE_DIR/providers_health.json"
  cp "$DEVICES_JSON" "$CAPTURE_DIR/devices.json"
  cp "$STATE_JSON" "$CAPTURE_DIR/state.json"

  {
    echo "base_url=$BASE_URL"
    echo "expected_providers=$EXPECTED_PROVIDERS"
    echo "allow_unavailable=$ALLOW_UNAVAILABLE"
    echo "min_device_count=$MIN_DEVICE_COUNT"
    echo "total_device_count=$TOTAL_DEVICES"
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > "$CAPTURE_DIR/summary.txt"

  echo "  captured artifacts: $CAPTURE_DIR"
fi
