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
- **Crash = unavailable**: Runtime marks devices offline (supervision may restart)
- **No stdin spam**: Only respond to requests
- **Quality matters**: Report STALE/FAULT when hardware fails

## Provider Supervision

The runtime can automatically monitor and restart crashed providers:

```yaml
providers:
  - id: hardware
    command: ./my-provider
    restart_policy:
      enabled: true
      max_attempts: 3
      backoff_ms: [200, 500, 1000]
      timeout_ms: 30000
```

### Crash Detection

The supervisor detects provider crashes when:

- Process exits unexpectedly
- ADPP operations timeout repeatedly
- Provider becomes unresponsive

### Restart Flow

1. **Crash Detected**: Supervisor logs crash with attempt counter
2. **Backoff Wait**: Delays restart according to `backoff_ms[attempt - 1]`
3. **Device Cleanup**: Clears all devices from registry before restart
4. **Process Restart**: Spawns new provider process
5. **Device Rediscovery**: Runs Hello → ListDevices → DescribeDevice for each device
6. **Recovery Tracking**: Resets crash counter on successful restart

### Circuit Breaker

After `max_attempts` consecutive crashes, the circuit breaker opens:

- No further automatic restarts
- Devices remain unavailable
- Manual intervention required (restart runtime or fix provider)

The circuit breaker resets when the provider successfully recovers.

### Backoff Strategy

The `backoff_ms` array defines delays before each restart attempt:

```yaml
# Conservative: Long delays for stable hardware
backoff_ms: [1000, 3000, 5000]

# Aggressive: Quick recovery for transient issues
backoff_ms: [100, 200, 500]

# Production: Balanced approach
backoff_ms: [200, 500, 1000]
```

### Best Practices

- **Enable for production providers**: Hardware can fail, supervision ensures resilience
- **Disable for development**: Crashes during development should stop execution for debugging
- **Tune backoff delays**: Match your hardware's restart characteristics
- **Set reasonable max_attempts**: Avoid infinite restart loops for permanently failed hardware
- **Monitor circuit breaker**: Alert when circuit opens (indicates persistent provider failure)

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
