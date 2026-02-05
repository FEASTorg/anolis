# Anolis Automation Layer

Phase 7 automation components for behavior tree orchestration.

## Architecture Constraints (Critical)

The automation layer is a **consumer of kernel services**, NOT a replacement for core IO:

| Constraint                                | Implementation                                               |
| ----------------------------------------- | ------------------------------------------------------------ |
| **BT nodes read via StateCache**          | No direct provider access; all state via blackboard snapshot |
| **BT nodes act via CallRouter**           | All device calls go through validated control path           |
| **No new provider protocol features**     | Automation uses existing ADPP v0 capabilities                |
| **No device-specific logic in BT engine** | BT runtime is capability-agnostic                            |

## Blackboard Contract (Phase 7A.2)

### Critical Semantics

- **BT sees tick-consistent snapshot**, NOT continuous state
- BT logic is edge-triggered by events, but **state visibility is per-tick**
- **No mid-tick state changes** visible to BT nodes
- BT is **NOT for hard real-time control**; call latency is acceptable

This design prevents incorrect assumptions about "reacting instantly" to mid-tick changes.

### Blackboard Schema

Populated before each tick in `BTRuntime::populate_blackboard()`:

```cpp
// StateCache reference (BT nodes will query on demand)
blackboard->set("state_cache", static_cast<void*>(&state_cache_));

// CallRouter reference (for device calls)
blackboard->set("call_router", static_cast<void*>(&call_router_));

// Parameters (Phase 7C)
blackboard->set("parameters", static_cast<void*>(&parameter_manager_));
```

**Important:** We pass **references**, not full snapshots, for efficiency. StateCache's `get_signal_value()` is thread-safe. This design is acceptable because:

1. Polling happens every 500ms, ticks every 100ms (10 Hz)
2. BT execution is fast compared to poll rate
3. If a value changes mid-tick, next tick will see the change
4. BT is for orchestration policy, not hard real-time control

## Thread Model

- **Single-threaded tick loop** in dedicated thread
- Tick rate configurable (default 10 Hz = 100ms period)
- Sleep until next tick (not busy-wait)
- BT nodes may **block on device calls** - trees must be designed for call latency

## Custom Nodes (Phase 7A.3)

Base classes for Anolis-specific BT nodes:

- `ReadSignalNode` - Reads from StateCache via blackboard
- `CallDeviceNode` - Invokes device function via CallRouter
- `CheckQualityNode` - Verifies signal quality (OK/STALE/FAULT)

All nodes registered with BehaviorTree.CPP factory.

## Safety Disclaimer

**Phase 7 automation is a control policy layer, not a safety-rated system.**

External safety systems (e.g., E-stops, interlocks) are still required for real hardware.

FAULT mode is _policy_, not a certified safety mechanism.

## Phase Status

- **Phase 7A**: BT Engine Foundation ✅ COMPLETE
  - 7A.1: Library integration ✅
  - 7A.2: Blackboard design ✅
  - 7A.3: Custom node API ✅
  - 7A.4: BT Runtime lifecycle ✅
  - 7A.5: Integration with Runtime ✅
  - 7A.6: Demo BT XML ✅
  - 7A.7: Documentation ✅

- **Phase 7B**: Runtime Modes & Gating ✅ COMPLETE
  - 7B.1: Mode state machine ✅
  - 7B.2: Manual/auto contention policy ✅
  - 7B.3: BT lifecycle gating ✅
  - 7B.4: HTTP API for mode control ✅
  - 7B.5: Mode change events ✅
  - 7B.6: Integration testing ✅
  - 7B.7: Documentation ✅

- **Phase 7C**: Parameters & Configuration ✅ COMPLETE
  - 7C.1: Parameter schema design ✅
  - 7C.2: YAML parameter configuration ✅
  - 7C.3: BT blackboard integration ✅
  - 7C.4: HTTP endpoints (GET/POST /v0/parameters) ✅
  - 7C.5: Parameter change telemetry ✅
  - 7C.6: Demo BT with parameters ✅
  - 7C.7: Testing ✅

Demo behavior tree available at: `behaviors/demo.xml`
Comprehensive documentation: `docs/automation.md`

Enable automation in `anolis-runtime.yaml`:

```yaml
automation:
  enabled: true
  behavior_tree: ./behaviors/demo.xml
  tick_rate_hz: 10
  manual_gating_policy: BLOCK # or OVERRIDE
```

## References

- [BehaviorTree.CPP Documentation](https://www.behaviortree.dev/)
