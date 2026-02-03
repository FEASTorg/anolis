# Anolis Operator UI

A **minimal dev/operator tool** for validating the Anolis HTTP API.

> **Note**: This is NOT a production UI. It is a dev-only sanity tool proving the API is human-operable.

## Quick Start

### 1. Start the Anolis Runtime

```bash
cd /path/to/anolis/build/core/Release
./anolis-runtime --config /path/to/anolis-runtime.yaml
```

### 2. Open the UI

**Option A: Direct file open** (may have CORS issues with some browsers)

```bash
# macOS
open tools/operator-ui/index.html

# Linux
xdg-open tools/operator-ui/index.html

# Windows
start tools/operator-ui/index.html
```

**Option B: Simple HTTP server** (recommended, avoids CORS)

```bash
# Python 3
python -m http.server 3000 -d tools/operator-ui

# Then open http://localhost:3000
```

**Option C: VS Code Live Server**

1. Install the "Live Server" extension
2. Right-click `index.html` → "Open with Live Server"

## Features

### Device List

- Shows all devices from `GET /v0/devices`
- Grouped by provider
- Click to select and view details

### State View

- Live-updating state via `GET /v0/state/{provider}/{device}`
- Polls every 500ms (configurable in code)
- Shows signal values with type, quality badge, and age
- Pause/Resume polling button

### Function Invocation

- Auto-generated forms from `GET /v0/devices/{provider}/{device}/capabilities`
- Type-aware input fields (double, int64, uint64, bool, string, bytes)
- Executes via `POST /v0/call`
- Shows success/error feedback

### Runtime Status

- Header shows runtime connection status
- Green = connected, Red = unavailable

## Configuration

Edit `app.js` to change:

```javascript
const API_BASE = "http://localhost:8080"; // Runtime HTTP address
const POLL_INTERVAL_MS = 500; // State polling interval
```

## Design Constraints

This UI follows strict constraints by design:

| Constraint             | Reason                        |
| ---------------------- | ----------------------------- |
| No framework           | Keep it simple, no build step |
| No npm/node            | Zero dependencies             |
| Capability-driven only | No device-type assumptions    |
| No charts/graphs       | Grafana territory             |
| No auth                | Phase 8                       |
| No streaming           | Phase 5 (SSE/WebSocket)       |

The UI is a **mirror** of the HTTP API, not a new abstraction. It must not introduce new semantics.

## Files

```
tools/operator-ui/
├── index.html   # Single-page UI structure
├── app.js       # Vanilla JS application logic
├── style.css    # Minimal dark theme styling
└── README.md    # This file
```

## Troubleshooting

### "Failed to connect to runtime"

- Ensure `anolis-runtime` is running
- Check it's bound to `127.0.0.1:8080`
- Check browser console for CORS errors

### CORS Errors

- Use Option B (python HTTP server) or Option C (Live Server)
- Or disable CORS in your browser for local dev (not recommended)

### State not updating

- Check the "Polling" status in the UI
- Click "Resume" if paused
- Check browser console for errors

## API Endpoints Used

| Endpoint                               | Purpose                  |
| -------------------------------------- | ------------------------ |
| `GET /v0/runtime/status`               | Connection health check  |
| `GET /v0/devices`                      | Device discovery         |
| `GET /v0/devices/{p}/{d}/capabilities` | Capability introspection |
| `GET /v0/state/{p}/{d}`                | Live state polling       |
| `POST /v0/call`                        | Function invocation      |
