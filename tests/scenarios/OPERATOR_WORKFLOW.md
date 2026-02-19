# Operator Workflow - Manual Validation Guide

This guide provides step-by-step workflows for manually validating the Anolis runtime system through the Operator UI and curl-based interactions.

## Prerequisites

- Anolis runtime built and available at `build/core/Release/anolis-runtime.exe` (or `Debug`)
- Provider simulator built at `../anolis-provider-sim/build/Release/anolis-provider-sim.exe`
- Python 3.9+ with `requests` library installed
- Web browser (Chrome, Firefox, or Edge)

## Workflow 1: UI-Based Validation (Recommended)

### Step 1: Start Runtime

```bash
# From project root (anolis/)
python tests/scenarios/run_scenarios.py --start-only

# Expected output:
# Starting runtime (will leave running)...
# Runtime started successfully (PID 12345)
# Base URL: http://localhost:8080
#
# To stop: python run_scenarios.py --stop
```

**What happens:**

- Runtime starts with automation enabled (demo.xml behavior tree)
- Provider simulator starts with 4 devices (tempctl0, motorctl0, relayio0, analogsensor0)
- HTTP server listens on port 8080
- Runtime begins in MANUAL mode (manual gating policy: BLOCK)

### Step 2: Open Operator UI

1. Open browser to: `http://localhost:8080`
2. **Expected:** UI loads with "Anolis Control Dashboard" header
3. **Verify:** Runtime status badge shows "OK" (green)
4. **Verify:** Stream badge shows "Connected" (green)

### Step 3: Verify Device List

1. Navigate to "Dashboard" tab (should be default)
2. **Expected:** Device Status Overview section shows 4 device cards:
   - `tempctl0` (Temperature Controller)
   - `motorctl0` (Motor Controller)
   - `relayio0` (Relay I/O)
   - `analogsensor0` (Analog Sensor)
3. **Verify:** Each card shows signal count, quality badge, and device type
4. Click any device card
5. **Expected:** Navigation to Devices tab with that device selected

### Step 4: Verify Automation Status Panel

1. In Dashboard tab, locate "Automation Control" section at top
2. **Expected fields:**
   - Current Mode: MANUAL (gray badge)
   - Set Mode dropdown: Shows MANUAL, AUTO, IDLE options
   - Runtime Parameters section below
   - Behavior Tree section (shows tree structure)
   - Event Trace section (initially empty)

### Step 5: Switch to AUTO Mode

1. Select "AUTO" from mode dropdown
2. Click "Set Mode" button
3. **Expected:**
   - "✓ Mode set" feedback appears briefly
   - Current Mode badge updates to "AUTO" (green)
   - Behavior tree begins ticking (runtime executes automation)
4. **Verify:** Event Trace shows mode change event:
   ```
   [time] mode_change  MANUAL → AUTO
   ```

### Step 6: Update Runtime Parameter

1. In "Runtime Parameters" section, locate `temp_setpoint` parameter
2. Current value should be 25.0, range [10.0, 50.0]
3. Change input field to `30.0`
4. Click "Set" button next to parameter
5. **Expected:**
   - "✓" checkmark appears briefly
   - Parameter value refreshes to 30.0
   - Event Trace shows parameter change:
     ```
     [time] parameter_change  temp_setpoint: 25.0 → 30.0
     ```
6. **Verify:** Behavior tree reacts to new setpoint (monitors live state)

### Step 7: View Behavior Tree Visualization

1. Scroll to "Behavior Tree" section in Automation Control
2. **Expected:** Text outline showing tree structure:
   ```
   BehaviorTree "demo"
   ├─ Sequence
      ├─ Condition "check_mode_auto"
      ├─ Action "read_temp_setpoint"
      └─ Sequence
         ├─ Action "read_current_temp"
         └─ Action "set_target_temp"
   ```
3. **Verify:** Tree structure matches loaded `behaviors/demo.xml`

### Step 8: Inject Fault (via Device Detail)

1. Navigate to "Devices" tab
2. Select `chaos_control` device from dropdown (special chaos engineering device)
3. Scroll to "Functions" section
4. Find `inject_fault` function
5. Set parameters:
   - `fault_type` = "UNAVAILABLE" (from dropdown)
6. Click "Call" button
7. **Expected:**
   - Function call succeeds
   - Runtime detects fault via health monitoring
   - Mode automatically transitions from AUTO → FAULT
   - Event Trace shows fault detection
   - Dashboard tab shows mode badge = "FAULT" (red)

### Step 9: Clear Fault and Recover

1. Still in `chaos_control` device detail
2. Find `clear_fault` function
3. Click "Call" button (no parameters needed)
4. **Expected:**
   - Fault cleared
   - Runtime remains in FAULT mode (manual recovery required)
5. Switch back to Dashboard tab
6. Select "MANUAL" from mode dropdown, click "Set Mode"
7. **Expected:**
   - Mode transitions FAULT → MANUAL
   - System ready for manual operation or return to AUTO

### Step 10: View Detailed Device State

1. Navigate to "Devices" tab
2. Select `tempctl0` from dropdown
3. **Verify Device Detail Panel shows:**
   - Device header with type badge and quality
   - Full state table with all signals:
     - `current_temp` (read-only, double)
     - `target_temp` (read-only, double)
     - `is_heating` (read-only, bool)
     - `sensor_quality` (read-only, string)
   - Each signal shows: Name, Value, Type, Quality, Timestamp
   - Functions section with `set_temp` function
4. Call `set_temp` function:
   - Set `temp` = 28.0
   - Click "Call"
   - **Expected:** Success, state updates to reflect new target

### Step 11: Monitor Real-Time State Updates (SSE)

1. Stay on Devices tab with `tempctl0` selected
2. Watch state table for live updates (every ~2 seconds)
3. **Verify:**
   - Timestamps update on each refresh
   - Values change as simulation runs (temperature changes)
   - Quality badges remain "OK" unless fault injected
4. In Dashboard tab, Event Trace shows continuous activity:
   - Parameter changes
   - State updates (if enabled in SSE filter)

### Step 12: View Telemetry (Grafana)

1. **Prerequisite:** Grafana + InfluxDB stack running (see SETUP docs)
2. Navigate to "Telemetry" tab
3. **Expected:**
   - Grafana dashboard embeds in iframe
   - Real-time charts showing device telemetry
   - If blocked: "Note" message with link to open in new tab
4. **Verify:** Telemetry data matches device state (temperature trends, relay states, etc.)

### Step 13: Stop Runtime

```bash
python tests/scenarios/run_scenarios.py --stop

# Expected output:
# Runtime stopped (PID 12345)
```

**Verify:**

- Browser UI shows "Runtime: Unknown" or connection error (expected)
- All processes terminated cleanly

---

## Workflow 2: curl-Based Validation (No UI)

Use this workflow to validate backend API without opening browser.

### Step 1: Start Runtime

```bash
python tests/scenarios/run_scenarios.py --start-only
```

### Step 2: Check Status

```bash
curl http://localhost:8080/v0/runtime/status
```

**Expected JSON:**

```json
{
  "status": {"code": "OK"},
  "runtime_mode": "MANUAL",
  "automation": {
    "behavior_tree": "demo.xml",
    "tick_rate_hz": 10,
    ...
  },
  ...
}
```

### Step 3: List Devices

```bash
curl http://localhost:8080/v0/devices
```

**Expected:** JSON array with 4 devices

### Step 4: Describe Device

```bash
curl http://localhost:8080/v0/devices/sim0/tempctl0
```

**Expected:** Device metadata with signals and functions

### Step 5: Read Device State

```bash
curl http://localhost:8080/v0/state/sim0/tempctl0
```

**Expected:** Current signal values

### Step 6: Switch to AUTO Mode

```bash
curl -X POST http://localhost:8080/v0/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": "AUTO"}'
```

**Expected:**

```json
{
  "status": { "code": "OK" },
  "mode": "AUTO"
}
```

### Step 7: Update Parameter

```bash
curl -X POST http://localhost:8080/v0/parameters \
  -H "Content-Type: application/json" \
  -d '{"name": "temp_setpoint", "value": 30.0}'
```

**Expected:**

```json
{
  "status": {"code": "OK"},
  "parameter": {
    "name": "temp_setpoint",
    "value": 30.0,
    ...
  }
}
```

### Step 8: Call Device Function

```bash
curl -X POST http://localhost:8080/v0/call \
  -H "Content-Type: application/json" \
  -d '{
    "provider_id": "sim0",
    "device_id": "tempctl0",
    "function_id": 2,
    "args": {
      "value": {"type": "double", "double": 28.0}
    }
  }'
```

**Note:** Function ID 2 is `set_setpoint` for tempctl device.

**Expected:**

```json
{
  "status": { "code": "OK" },
  "result": {}
}
```

### Step 9: Inject Fault

```bash
curl -X POST http://localhost:8080/v0/call \
  -H "Content-Type: application/json" \
  -d '{
    "provider_id": "sim0",
    "device_id": "chaos_control",
    "function_id": 1,
    "args": {
      "device_id": {"type": "string", "string": "tempctl0"},
      "duration_ms": {"type": "int64", "int64": 5000}
    }
  }'
```

**Note:** Function ID 1 is `inject_device_unavailable` for chaos_control device.

**Verify:** Next status check shows `runtime_mode: "FAULT"`

### Step 10: Clear Fault

```bash
curl -X POST http://localhost:8080/v0/call \
  -H "Content-Type: application/json" \
  -d '{
    "provider_id": "sim0",
    "device_id": "chaos_control",
    "function_id": 5,
    "args": {}
  }'
```

**Note:** Function ID 5 is `clear_faults` for chaos_control device.

### Step 11: Monitor Events (SSE Stream)

```bash
curl -N http://localhost:8080/v0/events
```

**Expected:** Continuous stream of events:

```
event: state_update
data: {"provider": "sim0", "device": "tempctl0", ...}

event: mode_change
data: {"previous_mode": "MANUAL", "new_mode": "AUTO", ...}

event: parameter_change
data: {"parameter_name": "temp_setpoint", "old_value": "25.0", ...}
```

Press Ctrl+C to stop stream.

### Step 12: Stop Runtime

```bash
python tests/scenarios/run_scenarios.py --stop
```

---

## Common Issues and Troubleshooting

### Issue: "Could not find anolis-runtime executable"

**Solution:**

1. Ensure runtime is built: `cd build && cmake --build . --config Release`
2. Specify explicit path: `python run_scenarios.py --runtime build/core/Release/anolis-runtime.exe --start-only`

### Issue: "Port 8080 already in use"

**Solution:**

1. Check for running instance: `python run_scenarios.py --stop`
2. Or use different port: `python run_scenarios.py --port 8081 --start-only`

### Issue: UI shows "Stream: Disconnected"

**Possible causes:**

- Runtime not running (check status badge)
- CORS or network issue (F12 console for errors)
- Polling fallback should activate automatically

**Solution:**

- Refresh page
- Check runtime logs for SSE connection errors
- Verify runtime status: `curl http://localhost:8080/v0/runtime/status`

### Issue: "No devices found" in UI

**Possible causes:**

- Provider not started
- Provider crashed during startup
- Device discovery failed

**Solution:**

1. Check runtime logs in console where `--start-only` was run
2. Verify provider process running: `ps aux | grep anolis-provider-sim` (Linux) or Task Manager (Windows)
3. Check API directly: `curl http://localhost:8080/v0/devices`

### Issue: Automation not working (mode stuck in MANUAL)

**Possible causes:**

- Manual gating policy is BLOCK (default, requires manual mode change)
- Automation not enabled in config

**Solution:**

- This is expected behavior with BLOCK policy
- Switch to AUTO via UI or curl to enable automation
- If AUTO mode fails, check BT file exists: `behaviors/demo.xml`

### Issue: Function call returns "INVALID_ARGUMENT"

**Possible causes:**

- Wrong argument type (string instead of double)
- Argument name mismatch
- Value out of constraint range

**Solution:**

1. Check device descriptor: `curl http://localhost:8080/v0/devices/sim0/tempctl0`
2. Verify function signature and constraints
3. Match exact argument names and types from descriptor

### Issue: Telemetry tab shows "blocked" message

**Possible causes:**

- Grafana not running
- Grafana X-Frame-Options not configured

**Solution:**

1. Start telemetry stack: See `docs/getting-started.md` telemetry section
2. Add to grafana config: `GF_SECURITY_ALLOW_EMBEDDING=true`
3. Restart Grafana
4. Alternative: Click "open in new tab" link

---

## Validation Checklist

Use this checklist to confirm all features working:

### Core Runtime

- [ ] Runtime starts without errors
- [ ] HTTP server responds on port 8080
- [ ] All 4 devices discovered (tempctl0, motorctl0, relayio0, analogsensor0)
- [ ] Device state readable via API
- [ ] Device functions callable via API

### Automation

- [ ] Runtime starts in MANUAL mode
- [ ] Mode changes to AUTO successfully
- [ ] Behavior tree ticks in AUTO mode (10Hz)
- [ ] Parameters readable and updatable
- [ ] Parameter changes propagate to BT
- [ ] Fault detection triggers FAULT mode
- [ ] Manual recovery from FAULT to MANUAL works

### Operator UI

- [ ] UI loads and shows device list
- [ ] Device cards clickable and navigate correctly
- [ ] Automation status panel shows current mode
- [ ] Mode selector and "Set Mode" button work
- [ ] Parameters panel shows all parameters
- [ ] Parameter update via UI works
- [ ] Behavior tree visualization renders
- [ ] Event trace shows real-time events
- [ ] Device detail view shows full state
- [ ] Function invocation from UI works
- [ ] SSE stream provides live updates
- [ ] Telemetry tab shows Grafana (if stack running)

### SSE Events

- [ ] state_update events received
- [ ] quality_change events received
- [ ] mode_change events received
- [ ] parameter_change events received
- [ ] device_availability events received (when devices appear/disappear)

### Fault Injection & Recovery

- [ ] inject_fault (UNAVAILABLE) triggers FAULT mode
- [ ] inject_fault (SIGNAL_FAULT) marks signals as BAD quality
- [ ] inject_fault (CALL_LATENCY) delays function calls
- [ ] clear_fault removes all injected faults
- [ ] System recovers gracefully after fault cleared

---

## Advanced Workflows

### Workflow 3: Extended Soak Test

Run the built-in soak test to validate stability:

```bash
# Run 30-minute soak test with monitoring
python tests/scenarios/run_scenarios.py --soak --duration 1800 --report soak-report.json
```

**Expected:**

- Runtime runs continuously for 30 minutes
- Memory growth <10%
- Thread count remains stable
- Periodic fault injection and parameter updates
- Final report shows PASS status

**Monitoring output every 5 minutes:**

```
[SOAK] 300s elapsed:
       Memory: 45.2 MB (growth: +2.3%)
       Threads: 12
       Faults injected: 10
       Parameters updated: 30
```

### Workflow 4: Automated Scenario Testing

Run all acceptance scenarios:

```bash
# Run all 11 scenarios with JSON report
python tests/scenarios/run_scenarios.py --report acceptance-report.json
```

**Expected:**

- All 11 scenarios pass
- Total runtime <2 minutes
- JSON report generated with structured results

View report:

```bash
cat acceptance-report.json | python -m json.tool
```

**Report structure:**

```json
{
  "start_time": "2026-02-08T10:30:00Z",
  "duration_seconds": 87.5,
  "scenarios": [
    {"name": "HappyPathEndToEnd", "status": "PASS", "duration_seconds": 5.2},
    ...
  ],
  "summary": {
    "total": 11,
    "passed": 11,
    "failed": 0,
    "pass_rate": 1.0
  }
}
```

---

## Integration with CI/CD

The scenario runner integrates with CI:

```yaml
# .github/workflows/ci.yml
- name: Run acceptance tests
  run: |
    python tests/scenarios/run_scenarios.py --report acceptance-report.json

- name: Upload report
  uses: actions/upload-artifact@v3
  with:
    name: acceptance-report
    path: acceptance-report.json
```

This ensures every commit is validated against the full acceptance test suite.

---

## Summary

This workflow guide demonstrates:

- ✅ Complete UI-based operator validation workflow
- ✅ Alternative curl-based API validation workflow
- ✅ Troubleshooting common issues
- ✅ Validation checklist for manual testing
- ✅ Advanced workflows (soak test, automated scenarios)
