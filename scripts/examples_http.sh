#!/bin/bash
# Anolis HTTP API - Curl Examples
# Usage: ./examples_http.sh [base_url]
#
# Demonstrates all HTTP API endpoints

BASE_URL="${1:-http://127.0.0.1:8080}"

echo "========================================"
echo "Anolis HTTP API Examples"
echo "Base URL: $BASE_URL"
echo "========================================"

# Helper function
api() {
    local method=$1
    local path=$2
    local data=$3
    
    echo ""
    echo "----------------------------------------"
    echo "$method $path"
    echo "----------------------------------------"
    
    if [ "$method" = "GET" ]; then
        curl -s "$BASE_URL$path" | jq .
    else
        curl -s -X "$method" "$BASE_URL$path" \
            -H "Content-Type: application/json" \
            -d "$data" | jq .
    fi
}

# 1. Runtime Status
echo ""
echo "=== 1. Runtime Status ==="
api GET /v0/runtime/status

# 2. List Devices
echo ""
echo "=== 2. List Devices ==="
api GET /v0/devices

# 3. Device Capabilities
echo ""
echo "=== 3. Temperature Controller Capabilities ==="
api GET /v0/devices/sim0/tempctl0/capabilities

echo ""
echo "=== 4. Motor Controller Capabilities ==="
api GET /v0/devices/sim0/motorctl0/capabilities

# 4. Get All State
echo ""
echo "=== 5. Get All State ==="
api GET /v0/state

# 5. Get Single Device State
echo ""
echo "=== 6. Get Motor Controller State ==="
api GET /v0/state/sim0/motorctl0

# 6. Control Examples
echo ""
echo "=== 7. Set Motor 1 Duty to 50% ==="
api POST /v0/call '{
  "provider_id": "sim0",
  "device_id": "motorctl0",
  "function_id": 10,
  "args": {
    "motor_index": {"type": "int64", "int64": 1},
    "duty": {"type": "double", "double": 0.5}
  }
}'

echo ""
echo "=== 8. Set Motor 2 Duty to 75% ==="
api POST /v0/call '{
  "provider_id": "sim0",
  "device_id": "motorctl0",
  "function_id": 10,
  "args": {
    "motor_index": {"type": "int64", "int64": 2},
    "duty": {"type": "double", "double": 0.75}
  }
}'

echo ""
echo "=== 9. Set Temperature Setpoint to 45°C ==="
api POST /v0/call '{
  "provider_id": "sim0",
  "device_id": "tempctl0",
  "function_id": 2,
  "args": {
    "value": {"type": "double", "double": 45.0}
  }
}'

echo ""
echo "=== 10. Verify State Changed ==="
sleep 0.5
api GET /v0/state

echo ""
echo "========================================"
echo "Examples Complete"
echo "========================================"
