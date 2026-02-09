#!/usr/bin/env bash
# Anolis Development Environment Launcher
#
# One-command development setup with full observability stack.
# Starts: Runtime + Operator UI + InfluxDB + Grafana
#
# Usage:
#   ./scripts/dev.sh                 # Full stack
#   ./scripts/dev.sh --skip-infra    # No Docker (runtime + UI only)
#   ./scripts/dev.sh --no-ui         # No operator UI server
#   ./scripts/dev.sh --skip-build    # Don't check/rebuild if needed
#   ./scripts/dev.sh --config PATH   # Custom config file

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_header() { echo -e "${CYAN}$*${NC}"; }
print_success() { echo -e "${GREEN}✓ $*${NC}"; }
print_warning() { echo -e "${YELLOW}⚠ $*${NC}"; }
print_error() { echo -e "${RED}✗ $*${NC}"; }
print_step() { echo -e "${CYAN}► $*${NC}"; }

# Parse arguments
SKIP_BUILD=false
SKIP_INFRA=false
NO_UI=false
CONFIG=""
BUILD_DIR=""

while [[ $# -gt 0 ]]; do
	case $1 in
	--skip-build)
		SKIP_BUILD=true
		shift
		;;
	--skip-infra)
		SKIP_INFRA=true
		shift
		;;
	--no-ui)
		NO_UI=true
		shift
		;;
	--config)
		CONFIG="$2"
		shift 2
		;;
	--build-dir)
		BUILD_DIR="$2"
		shift 2
		;;
	*)
		echo "Unknown option: $1"
		exit 1
		;;
	esac
done

# Paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

[ -z "$BUILD_DIR" ] && BUILD_DIR="$REPO_ROOT/build"
[ -z "$CONFIG" ] && CONFIG="$REPO_ROOT/anolis-runtime.yaml"

DOCKER_DIR="$REPO_ROOT/tools/docker"
UI_DIR="$REPO_ROOT/tools/operator-ui"

# Cleanup tracking
UI_SERVER_PID=""
RUNTIME_PID=""
CLEANED_UP=false

# ============================================================================
# Cleanup Handler
# ============================================================================
cleanup() {
	if [ "$CLEANED_UP" = true ]; then
		return
	fi
	CLEANED_UP=true

	echo ""
	print_step "Shutting down..."

	if [ -n "$UI_SERVER_PID" ]; then
		print_step "Stopping operator UI server..."
		kill "$UI_SERVER_PID" 2>/dev/null || true
		wait "$UI_SERVER_PID" 2>/dev/null || true
		print_success "UI server stopped"
	fi

	if [ -n "$RUNTIME_PID" ]; then
		print_step "Stopping runtime..."
		kill "$RUNTIME_PID" 2>/dev/null || true
		wait "$RUNTIME_PID" 2>/dev/null || true
		print_success "Runtime stopped"
	fi

	if [ "$SKIP_INFRA" = false ]; then
		print_step "Stopping Docker stack..."
		(cd "$DOCKER_DIR" && docker compose -f docker-compose.observability.yml down >/dev/null 2>&1)
		print_success "Docker stack stopped"
	fi

	echo ""
	print_success "Cleanup complete"
}

trap 'cleanup; exit 130' SIGINT
trap 'cleanup; exit 143' SIGTERM
trap cleanup EXIT

# ============================================================================
# Header
# ============================================================================
echo ""
echo -e "${CYAN}╔════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║  Anolis Development Environment                ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════╝${NC}"
echo ""

# ============================================================================
# Step 1: Validate Build
# ============================================================================
print_step "Checking build..."

# Try Release, then Debug, then single-config
RUNTIME="$BUILD_DIR/core/Release/anolis-runtime"
[ ! -f "$RUNTIME" ] && RUNTIME="$BUILD_DIR/core/Debug/anolis-runtime"
[ ! -f "$RUNTIME" ] && RUNTIME="$BUILD_DIR/core/anolis-runtime"

if [ ! -f "$RUNTIME" ]; then
	print_warning "Runtime not built"

	if [ "$SKIP_BUILD" = true ]; then
		print_error "Runtime not found and --skip-build specified"
		exit 1
	fi

	read -p "Build now? (Y/n): " response
	if [[ "$response" =~ ^[Nn]$ ]]; then
		print_error "Cannot continue without runtime"
		exit 1
	fi

	print_step "Building..."
	"$SCRIPT_DIR/build.sh"

	# Re-check after build
	RUNTIME="$BUILD_DIR/core/Release/anolis-runtime"
	[ ! -f "$RUNTIME" ] && RUNTIME="$BUILD_DIR/core/Debug/anolis-runtime"
	[ ! -f "$RUNTIME" ] && RUNTIME="$BUILD_DIR/core/anolis-runtime"

	if [ ! -f "$RUNTIME" ]; then
		print_error "Build succeeded but runtime not found"
		exit 1
	fi
fi

print_success "Runtime found: $RUNTIME"

# Validate config
if [ ! -f "$CONFIG" ]; then
	print_error "Config file not found: $CONFIG"
	exit 1
fi
print_success "Config found: $CONFIG"

# ============================================================================
# Step 2: Start Infrastructure (Optional)
# ============================================================================
if [ "$SKIP_INFRA" = false ]; then
	echo ""
	print_step "Starting observability stack (InfluxDB + Grafana)..."

	# Check Docker
	if ! command -v docker &>/dev/null; then
		print_warning "Docker not found - skipping infrastructure"
		print_warning "Install Docker to enable telemetry visualization"
		SKIP_INFRA=true
	else
		(cd "$DOCKER_DIR" && docker compose -f docker-compose.observability.yml up -d >/dev/null 2>&1)

		if [ $? -eq 0 ]; then
			print_success "Docker stack started"

			# Wait for health check
			print_step "Waiting for InfluxDB to be ready..."
			waited=0
			healthy=false
			while [ $waited -lt 30 ] && [ "$healthy" = false ]; do
				sleep 2
				waited=$((waited + 2))

				status=$(cd "$DOCKER_DIR" && docker compose -f docker-compose.observability.yml ps --format json 2>/dev/null)
				if echo "$status" | grep -q '"Health":"healthy"'; then
					healthy=true
				fi
			done

			if [ "$healthy" = true ]; then
				print_success "InfluxDB ready"
			else
				print_warning "InfluxDB health check timeout (may still be starting)"
			fi
		else
			print_warning "Failed to start Docker stack"
			SKIP_INFRA=true
		fi
	fi
else
	print_warning "Skipping infrastructure (--skip-infra)"
fi

# ============================================================================
# Step 3: Start Operator UI Server (Optional)
# ============================================================================
if [ "$NO_UI" = false ]; then
	echo ""
	print_step "Starting operator UI server (port 3000)..."

	# Check Python
	PYTHON_CMD=""
	if command -v python3 &>/dev/null; then
		PYTHON_CMD="python3"
	elif command -v python &>/dev/null; then
		PYTHON_CMD="python"
	fi

	if [ -n "$PYTHON_CMD" ]; then
		# Start Python HTTP server in background
		(cd "$UI_DIR" && $PYTHON_CMD -m http.server 3000 >/dev/null 2>&1) &
		UI_SERVER_PID=$!

		sleep 1

		if kill -0 $UI_SERVER_PID 2>/dev/null; then
			print_success "Operator UI server started at http://localhost:3000"
		else
			print_warning "Failed to start UI server"
			UI_SERVER_PID=""
		fi
	else
		print_warning "Python not found - skipping UI server"
		print_warning "Install Python 3 to enable operator UI"
	fi
else
	print_warning "Skipping operator UI (--no-ui)"
fi

# ============================================================================
# Step 4: Print Dashboard
# ============================================================================
echo ""
echo -e "${GREEN}╔════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  Development Environment Ready                 ║${NC}"
echo -e "${GREEN}╠════════════════════════════════════════════════╣${NC}"
echo -e "║  Runtime:  http://127.0.0.1:8080              ║"

if [ "$NO_UI" = false ] && [ -n "$UI_SERVER_PID" ]; then
	echo -e "║  Operator: http://localhost:3000              ║"
fi

if [ "$SKIP_INFRA" = false ]; then
	echo -e "║  Grafana:  http://localhost:3001 (admin/...)  ║"
	echo -e "║  InfluxDB: http://localhost:8086 (admin/...)  ║"
fi

echo -e "${GREEN}╠════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║  Press Ctrl+C to stop                          ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════╝${NC}"
echo ""

# ============================================================================
# Step 5: Start Runtime (Foreground)
# ============================================================================
print_step "Starting Anolis runtime..."
echo ""

# Start runtime in foreground
"$RUNTIME" "--config=$CONFIG" &
RUNTIME_PID=$!

# Wait for runtime to exit
wait $RUNTIME_PID
EXIT_CODE=$?

# Cleanup will be called by trap
exit $EXIT_CODE
