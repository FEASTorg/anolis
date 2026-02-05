# Scenario Testing Debugging Summary

**Date:** February 5, 2026  
**Status:** In Progress

## Overview

This document summarizes the debugging work done to get the Phase 8 validation scenario suite working correctly with the anolis runtime and provider-sim.

---

## Issues Found and Fixed

### 1. Endpoint URL Mismatch

**File:** `scenarios/base.py` → `set_mode()`  
**Issue:** Scenario was calling `/v0/runtime/mode` but API endpoint is `/v0/mode`  
**Fix:** Changed URL from `/v0/runtime/mode` to `/v0/mode`

### 2. Windows Unicode Encoding

**File:** `scripts/run_scenarios.py`  
**Issue:** CI on Windows failed with `UnicodeEncodeError` when printing ✓/✗ characters (cp1252 encoding)  
**Fix:** Added UTF-8 encoding wrapper for stdout/stderr on Windows:

```python
if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')
```

### 3. Missing Behavior Tree Path

**File:** `scripts/run_scenarios.py`  
**Issue:** Runtime config had `automation.enabled: true` but no `behavior_tree` path, causing startup failure  
**Fix:** Added `behaviors/test_noop.xml` and configured it in the scenario runner's config generation

### 4. Function Call JSON Format

**File:** `scenarios/base.py` → `call_function()`  
**Issue:** Scenarios sent function calls with wrong format:

- Used function name string instead of function_id integer
- Args were plain Python values instead of typed format
  **Fix:** Rewrote `call_function()` to:
- Look up function_id from capabilities when given a name
- Convert Python values to typed format: `{"type": "int64", "int64": 123}`

### 5. Capabilities Response Nesting

**File:** `scenarios/base.py` → `get_capabilities()`  
**Issue:** API returns `{"status": {...}, "capabilities": {...}}` but scenarios expected capabilities at top level  
**Fix:** Extract inner `capabilities` object: `return data.get("capabilities", data)`

### 6. State Response Format

**File:** `scenarios/base.py` → `get_state()`  
**Issue:**

- API returns `"values"` array, scenarios expected `"signals"`
- API returns typed values `{"type": "double", "double": 1.23}`, scenarios expected plain values
  **Fix:** Normalize response:
- Rename `values` → `signals`
- Extract actual values from typed format

### 7. Status Response Format

**File:** `scenarios/base.py` → `call_function()`  
**Issue:** API returns `{"status": {"code": "OK", "message": "..."}}` but scenarios checked `result["status"] == "OK"`  
**Fix:** Normalize status in `call_function()`:

```python
if isinstance(result.get("status"), dict):
    result["status"] = result["status"].get("code", result["status"])
```

### 8. Runtime Status Hardcoded Mode

**File:** `core/http/handlers.cpp` → `handle_get_runtime_status()`  
**Issue:** Endpoint always returned `"mode": "MANUAL"` regardless of actual mode  
**Fix:** Query actual mode from mode_manager:

```cpp
std::string current_mode = "MANUAL";
if (mode_manager_) {
    current_mode = automation::mode_to_string(mode_manager_->current_mode());
}
```

### 9. Device Registry Logging After Move

**File:** `core/registry/device_registry.cpp`  
**Issue:** Log statement accessed `reg_device` after `std::move()`, showing 0 signals/functions  
**Fix:** Move logging before the `std::move()` call

---

## Commits Made

1. **865b93b** - "Fix scenario/API compatibility issues"
   - URL fix, typed args, status normalization, Windows encoding, behavior_tree config

2. **00ee460** - "Fix runtime status endpoint and scenario status checks"
   - Runtime status returns actual mode, simplified scenario status checks

---

## Current Scenario Status

### Passing (4/11)

| Scenario           | Duration | Notes                                                 |
| ------------------ | -------- | ----------------------------------------------------- |
| TestInfrastructure | ~12s     | Basic connectivity and API test                       |
| HappyPathEndToEnd  | ~23s     | Full device flow: discover → state → control → verify |
| ModeBlockingPolicy | ~29s     | MANUAL→AUTO→MANUAL mode switching, call blocking      |
| TelemetryOnChange  | ~40s     | State change events via SSE                           |

### Failing (7/11)

| Scenario                | Error         | Root Cause                                                      |
| ----------------------- | ------------- | --------------------------------------------------------------- |
| FaultToManualRecovery   | ReadTimeout   | Runtime hangs on `inject_device_unavailable` call               |
| ProviderRestartRecovery | ReadTimeout   | Same fault injection issue                                      |
| MultiDeviceConcurrency  | ReadTimeout   | Rapid sequential calls cause timeouts                           |
| OverridePolicy          | HTTPError 500 | Config has BLOCK policy, test expects OVERRIDE behavior         |
| ParameterValidation     | HTTPError 500 | Type mismatch causes internal error instead of validation error |
| PreconditionEnforcement | Unknown       | Needs investigation                                             |
| SlowSseClientBehavior   | Unknown       | Needs investigation                                             |

---

## Remaining Work

### High Priority

1. **Fault Injection Hanging** - The `inject_device_unavailable` function causes the runtime to hang. Need to investigate:
   - Is the provider blocking on a call?
   - Is the runtime waiting for a response indefinitely?
   - Should there be a timeout on provider calls?

2. **Parameter Validation** - When scenarios pass invalid parameter types (e.g., string where int expected), the runtime returns HTTP 500 instead of a proper validation error. The `call_function` helper converts Python types to typed format, but the runtime's type checking should handle mismatches gracefully.

### Medium Priority

3. **Override Policy Test** - The scenario runner creates config with `manual_gating_policy: "BLOCK"`. Either:
   - Modify scenario to test with BLOCK policy, or
   - Add config option to use OVERRIDE policy for specific tests

4. **Sequential Operation Timeouts** - MultiDeviceConcurrency issues ~50 rapid calls. Some timeout. May need:
   - Longer timeouts for concurrent scenarios
   - Rate limiting in test
   - Investigation of provider throughput

### Lower Priority

5. **Investigate remaining scenarios** - PreconditionEnforcement and SlowSseClientBehavior need individual testing to identify failure modes

---

## CI Considerations

The CI workflow runs all scenarios, so failing scenarios will fail the build. Options:

1. **Skip known-broken scenarios** - Add `--skip` flag to scenario runner
2. **Mark scenarios as xfail** - Expected failure, don't fail build
3. **Fix all scenarios** - Most thorough but time-consuming
4. **Run only passing scenarios** - Add `--scenario` filter for CI

Recommended: Add skip mechanism for CI while fixing scenarios incrementally.

---

## Submodule Workflow

When making changes to anolis that affect scenarios:

1. Make changes in `anolis/`
2. Commit and push to anolis main
3. In `anolis-provider-sim/`:
   ```bash
   git submodule update --remote --merge external/anolis
   git add external/anolis
   git commit -m "Update anolis submodule to <commit>"
   git push
   ```

This ensures CI builds use the correct submodule version.

---

## Latest Verification Status (Run 2)

**Date:** February 5, 2026 (Post-Fixes)

### New Fixes Attempted

**10. Thread Safety in Fault Injection**

- **File:** `anolis-provider-sim/src/fault_injection.cpp`
- **Issue:** `FaultToManualRecovery` was causing the runtime to hang/timeout. Diagnostics suggested a race condition.
- **Fix:** Added `std::mutex` and `std::lock_guard` to protect all access to the static failure state maps.

**11. Error Mapping for Validation**

- **File:** `core/http/handlers.cpp`
- **Issue:** `ParameterValidation` scenario fails because runtime returns HTTP 500 for invalid args, but test expects HTTP 400.
- **Fix:** Updated `handle_post_call` to check for "invalid" and "missing" keywords in the error message and map them to `StatusCode::INVALID_ARGUMENT`.

### Current Results

Despite the fixes above, the following issues persist:

1.  **FaultToManualRecovery**: Still fails with `ReadTimeout` (~32s).
    - The mutex fix ensures thread safety, but the timeout persists.
    - **Hypothesis:** The provider might be simulating the "unavailable" state by actually not responding (blocking), or the test timeout (10s) is insufficient for the 5s injected fault.

2.  **ParameterValidation**: Still fails with `HTTP 500`.
    - The runtime _should_ be returning 400 now.
    - **Hypothesis:** The error message string from the provider might not match the keywords as expected, or the error is coming from `CallRouter` validation (which throws a generic error), or the build didn't update correctly.

**Next Steps:**

1.  Verify the exact error string returned by the provider using debug prints.
2.  Double-check the build artifact currency.
