# Providers

## What is a Provider?

A **provider** is a small executable that:

1. Speaks to specific hardware (sensors, actuators, PLCs, etc.)
2. Exposes devices via ADPP (Anolis Device Provider Protocol)
3. Runs as an isolated process (stdio communication)

## Provider Protocol (ADPP)

Communication via **stdin/stdout** with **uint32 length-prefixed protobuf messages**.

### Message Flow

```text
Runtime ────┐
            │ Request (protobuf, length-prefixed)
            ▼
         Provider
            │
            ▼ Response (protobuf, length-prefixed)
Runtime ◄───┘
```

### Five Operations

1. **Hello**: Provider identifies itself
2. **ListDevices**: Returns device IDs
3. **DescribeDevice**: Returns capabilities (signals, functions)
4. **ReadSignals**: Returns current signal values
5. **Call**: Executes function (e.g., set_relay, move_motor)

See `external/anolis/spec/device-provider/protocol.proto` for schema.

## Example: anolis-provider-sim

Simulated provider with:

- **tempctl0**: Temperature controller (2 relays, temp/humidity sensors)
- **motorctl0**: Motor controller (speed, position, status)

Source: <https://github.com/FEASTorg/anolis-provider-sim>

Key files:

- `src/main.cpp`: stdio framing + message dispatch
- `src/handlers.cpp`: ADPP operation handlers
- `src/sim_devices.cpp`: Device state simulation

## Creating a Provider

### Minimal Steps

1. **Pick a language**: C++, Rust, Python - anything that can do stdio + protobuf
2. **Implement framing**: Read/write uint32_le length prefix
3. **Implement 5 ADPP handlers**: Hello, ListDevices, DescribeDevice, ReadSignals, Call
4. **Handle hardware**: Your code, your protocol (Modbus, SPI, etc.)

### Provider Template (pseudocode)

```cpp
while (true) {
    Request req = read_framed_stdin();

    Response resp;
    switch (req.type) {
        case HELLO:
            resp = handle_hello();
            break;
        case LIST_DEVICES:
            resp = handle_list_devices();
            break;
        case DESCRIBE_DEVICE:
            resp = handle_describe(req.device_id);
            break;
        case READ_SIGNALS:
            resp = handle_read(req.device_id, req.signal_ids);
            break;
        case CALL:
            resp = handle_call(req.device_id, req.function_id, req.args);
            break;
    }

    write_framed_stdout(resp);
}
```

### Rules

- **Stateless preferred**: Runtime caches state, you just read hardware
- **Blocking OK**: Runtime handles concurrency
- **Crash = unavailable**: Runtime marks devices offline
- **No stdin spam**: Only respond to requests
- **Quality matters**: Report STALE/FAULT when hardware fails

### Provider Internal State (Important)

Providers may maintain **ephemeral protocol state** required by hardware:

- Multi-step read sequences (e.g., CRUMBS staged reads: select → fetch)
- Communication buffers
- Hardware-specific state machines

Critical boundary:

- ✅ **Provider internal state**: Protocol implementation details
- ✅ **Core single source of truth**: Machine state visible to rest of system (StateCache)
- ❌ **Never**: Expose provider state directly to UIs/automation

Example: CRUMBS provider may buffer staged reads internally, but Anolis core remains authoritative for what the "current temperature" is.

## Provider Examples (Planned)

- `anolis-provider-modbus`: Modbus RTU/TCP devices
- `anolis-provider-arduino`: Arduino via serial
- `anolis-provider-canbus`: CAN bus devices
- `anolis-provider-crumbs`: FEAST CRUMBS integration
- `anolis-provider-ni`: National Instruments DAQ

## Testing Your Provider

Use `anolis-runtime`:

```yaml
# anolis-runtime.yaml
providers:
  - id: my_provider
    command: /path/to/my-provider
    args: ["--port", "/dev/ttyUSB0"]
```

Run and check logs for discovery, polling, and control operations.

## Provider Isolation Benefits

- **Crash safety**: Provider crash doesn't kill runtime
- **Language freedom**: Use best tool for hardware
- **Security**: No shared memory, limited blast radius
- **Testing**: Mock providers for CI/CD
