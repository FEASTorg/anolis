# Anolis Automation Layer

Phase 7 automation components for behavior tree orchestration.

## Architecture Constraints (Critical)

The automation layer is a **consumer of kernel services**, NOT a replacement for core IO:

| Constraint | Implementation |
|-----------|---------------|
| **BT nodes read via StateCache** | No direct provider access; all state via blackboard snapshot |
| **BT nodes act via CallRouter** | All device calls go through validated control path |
| **No new provider protocol features** | Automation uses existing ADPP v0 capabilities |
| **No device-specific logic in BT engine** | BT runtime is capability-agnostic |

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
// StateCache snapshot
blackboard->set("signal_<id>", value);          // Signal values
blackboard->set("quality_<id>", quality);       // Signal quality (OK/STALE/FAULT)

// CallRouter reference (for device calls)
blackboard->set("call_router", &call_router_);

// Parameters (Phase 7C)
blackboard->set("parameters", &parameter_manager_);
```

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

FAULT mode is *policy*, not a certified safety mechanism.

## Phase Status

- **Phase 7A**: BT Engine Foundation (IN PROGRESS)
  - 7A.1: Library integration ✅
  - 7A.2: Blackboard design (NEXT)
  - 7A.3: Custom node API (TODO)
  - 7A.4: BT Runtime lifecycle (TODO)
  - 7A.5: Integration with Runtime (TODO)
  - 7A.6: Demo BT (TODO)
  - 7A.7: Testing & CI (TODO)

- **Phase 7B**: Runtime Modes & Gating (PLANNED)
- **Phase 7C**: Parameters & Configuration (PLANNED)

## References

- [BehaviorTree.CPP Documentation](https://www.behaviortree.dev/)
- [Phase 7 Plan](../../working/phase_7_plan.md)
- [Master Plan](../../working/anolis_master_plan.md)
