# anolis

**Anolis** is a modular control runtime for building machines from heterogeneous devices.

It provides a **hardware-agnostic core** that discovers devices, understands their capabilities, maintains live state, and coordinates control actions — independent of how the hardware is connected or implemented.

Anolis is designed to sit **between low-level device interfaces and high-level behavior**, acting as the system’s operational brain.

> Just as anoles adapt to diverse environments,
> Anolis adapts to diverse hardware ecosystems.

---

## What Anolis Is

Anolis is:

- A **runtime kernel** for machines composed of many devices
- A **provider-based system**, where hardware integrations live out of process
- A **capability-driven control layer**, not hardcoded device logic
- A **bridge** between:
  - device buses (I²C, SPI, GPIO, CAN, BLE, etc.)
  - orchestration layers (dashboards, schedulers, behavior trees)

Anolis does **not** assume:

- a specific bus
- a specific MCU
- a specific control paradigm
- a specific UI

---

## Core Concepts

### Providers

A _provider_ is an external process that exposes one or more devices using a standard protocol.

Providers may:

- talk to microcontrollers
- wrap drivers or SDKs
- simulate hardware
- proxy other systems

Anolis communicates with providers over a simple message protocol and treats them as isolated, replaceable components.

---

### Devices

A _device_ represents a functional unit with:

- signals (telemetry / state)
- functions (actions or configuration)
- metadata and capabilities

Devices are **described**, not hardcoded.

---

### Capabilities

Capabilities define:

- what signals exist
- what functions can be called
- what arguments and constraints apply

Anolis uses capabilities to:

- validate control actions
- drive UIs automatically
- enable generic orchestration

---

### State

Anolis maintains a live, cached view of device state by polling providers.

All reads go through this cache.  
All control actions flow through a single validated path.

---

## What Anolis Is Not

Anolis is **not**:

- a device driver
- a hardware abstraction layer
- a dashboard
- a PLC replacement
- a firmware framework

Those concerns live _around_ Anolis, not inside it.

---

## Typical Stack

```text
[ Dashboards / APIs / Schedulers ]
↓
[ Anolis Core ]
↓
[ Providers ]
↓
[ Hardware / Systems ]
```

Examples:

- An HTTP dashboard issues a control request → Anolis validates → Provider executes
- A behavior tree reads state → Anolis cache → decides next action
- A simulation provider is swapped for real hardware with no core changes

---

## Design Goals

- **Hardware-agnostic**
- **Process-isolated integrations**
- **Explicit capabilities**
- **Deterministic control paths**
- **Composable systems**

Anolis is intended to scale from small experimental rigs to complex multi-device machines.

---

## Status

_Anolis_ is under active development.

Current focus:

- Core runtime
- Provider interface
- State and control infrastructure

Higher-level orchestration and UI layers build on top of this foundation.

---

## Name

_Anolis_ is named after [anoles](https://en.wikipedia.org/wiki/Dactyloidae) — adaptable lizards known for thriving across diverse environments.

The name reflects the system’s goal:  
**adaptable control over diverse hardware ecosystems.**
