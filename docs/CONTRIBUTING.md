# Contributing Guide

## Development Environment

See [getting-started.md](getting-started.md) for initial setup.

## CI/CD

Both `anolis` and `anolis-provider-sim` use GitHub Actions for CI. Tests run on:

- Ubuntu 22.04 (GCC)
- Windows Server 2022 (MSVC 2022)

### Running Tests Locally

```bash
# From anolis repo root
python scripts/test_all.py --timeout=60

# With explicit paths
python scripts/test_all.py \
  --runtime=build/core/Release/anolis-runtime.exe \
  --provider=../anolis-provider-sim/build/Release/anolis-provider-sim.exe
```

---

## Common Pitfalls & Solutions

### 1. Windows Macro Conflicts

**Problem**: Windows headers define macros like `min`, `max`, `GetTickCount` that conflict with C++ standard library and protobuf.

**Symptoms**:

```text
error C2039: 'GetTickCount': is not a member of 'google::protobuf::util::TimeUtil'
error C2589: '(': illegal token on right side of '::'
```

**Solution**: In any header that uses `std::min`, `std::max`, or protobuf time utilities:

```cpp
#pragma once

// MUST be before any Windows headers
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <algorithm>
#include <google/protobuf/util/time_util.h>

// Use parentheses to prevent macro expansion
double clamped = (std::max)(lo, (std::min)(hi, v));
auto ts = (google::protobuf::util::TimeUtil::GetCurrentTime)();
```

### 2. C++ Incomplete Type Errors

**Problem**: Forward declarations cause "incomplete type" errors when used in containers.

**Symptoms**:

```text
error: invalid use of incomplete type 'struct SignalSpec'
```

**Solution**: Define structs before they're used in container types:

```cpp
// WRONG - forward declaration used before definition
struct SignalSpec;
struct DeviceCapabilitySet {
    std::unordered_map<std::string, SignalSpec> signals;  // Error!
};
struct SignalSpec { ... };

// RIGHT - define before use
struct SignalSpec { ... };
struct DeviceCapabilitySet {
    std::unordered_map<std::string, SignalSpec> signals;  // OK
};
```

### 3. vcpkg Package Sharing Between Projects

**Problem**: Building `anolis-provider-sim` after `anolis` re-downloads packages.

**Solution**: Share `vcpkg_installed` directory:

```bash
# Build anolis first (creates vcpkg_installed/)
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Build provider-sim, reusing packages
cd ../anolis-provider-sim
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_INSTALLED_DIR=$ANOLIS_ROOT/build/vcpkg_installed \
  -DVCPKG_MANIFEST_INSTALL=OFF
```

### 4. Linux Process Cleanup (pkill)

**Problem**: `pkill -f name` matches the full command line, including paths like `/home/user/anolis/...`.
This can accidentally kill unrelated processes.

**Symptoms**:

- Test script kills itself (exit code 143 = SIGTERM)
- Python processes terminated unexpectedly

**Solution**: Use `-x` for exact binary name match:

```python
# WRONG - matches paths containing "anolis"
subprocess.run(["pkill", "-f", "anolis-runtime"])

# RIGHT - matches only processes named exactly "anolis-runtime"
subprocess.run(["pkill", "-x", "anolis-runtime"])
```

### 5. Bash Exit Code Capture

**Problem**: `cmd || VAR=$?` doesn't capture the command's exit code.

**Symptoms**: Exit code is always 0 or wrong value.

**Explanation**: The `||` means "run if previous failed", but `$?` then captures the assignment's exit code (always 0), not the command's.

**Solution**:

```bash
# WRONG
python test.py || TEST_EXIT=$?
if [ "${TEST_EXIT:-0}" -ne 0 ]; then ...

# RIGHT
set +e  # Don't exit on error
python test.py
TEST_EXIT=$?
if [ $TEST_EXIT -ne 0 ]; then ...
exit $TEST_EXIT
```

### 6. Python subprocess OOM on CI

**Problem**: `subprocess.run(capture_output=True)` buffers all output in memory, causing OOM when tests produce lots of output.

**Symptoms**: Exit code 137 (SIGKILL) on Linux CI, "Killed" in logs.

**Solution**: Stream output to log files:

```python
# WRONG - buffers everything in memory
result = subprocess.run(cmd, capture_output=True)

# RIGHT - stream to file
with open("test.log", "w") as log_file:
    result = subprocess.run(cmd, stdout=log_file, stderr=subprocess.STDOUT)
```

### 7. GitHub Actions Permissions

**Problem**: Some diagnostic commands require elevated permissions.

**Symptoms**: `dmesg: read kernel buffer failed: Operation not permitted`

**Solution**: Silence permission errors, these are nice-to-have diagnostics:

```bash
dmesg 2>/dev/null | grep -i "oom\|killed" || true
```

---

## Signal Handling Safety

The Anolis runtime uses POSIX signal handlers for graceful shutdown (SIGINT/SIGTERM).
Signal handlers have **strict safety constraints** that must be followed.

### Async-Signal-Safe Requirements

According to POSIX standards, signal handlers may **only** call async-signal-safe functions.
Most C/C++ standard library functions are **NOT** safe, including:

- ❌ `malloc/free`, `new/delete` (heap allocation)
- ❌ `std::mutex::lock()`, `std::lock_guard` (mutexes)
- ❌ `LOG_INFO()`, `std::cout`, `printf()` (I/O operations)
- ❌ `std::function` callback invocation (may allocate)
- ✅ `std::atomic<T>::load/store()` (safe)
- ✅ Writing to `volatile sig_atomic_t` (safe, but atomic preferred)

**Violation Consequences**: Deadlocks, crashes, undefined behavior (especially under ThreadSanitizer/ASAN).

### Implementation Pattern

The runtime uses an **atomic flag polling pattern**:

```cpp
// signal_handler.hpp
class SignalHandler {
public:
    static void install();
    static bool is_shutdown_requested();
private:
    static void handle_signal(int signal);
    static std::atomic<bool> shutdown_requested_;
};

// signal_handler.cpp
std::atomic<bool> SignalHandler::shutdown_requested_{false};

void SignalHandler::handle_signal(int signal) {
    // ONLY atomic operations in signal context
    shutdown_requested_.store(true);
    // NO callbacks, NO logging, NO mutexes
}

bool SignalHandler::is_shutdown_requested() {
    return shutdown_requested_.load();
}

// Main loop polls the flag
while (running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (SignalHandler::is_shutdown_requested()) {
        LOG_INFO("Signal received, stopping...");  // Safe: not in signal context
        running_ = false;
        break;
    }
}
```

### Testing

Signal handling is validated by `tests/integration/test_signal_handling.py`:

```bash
python tests/integration/test_signal_handling.py
```

This test verifies:

- Clean shutdown on SIGINT (Ctrl+C)
- Clean shutdown on SIGTERM (kill)
- No hangs or deadlocks
- Shutdown completes within timeout

Run with ThreadSanitizer when available to detect data races:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DSANITIZER=thread
cmake --build build
python tests/integration/test_signal_handling.py --runtime=build/core/anolis-runtime
```

---

## Code Style

- **C++**: Modern C++17, prefer value semantics
- **Python**: PEP 8, type hints encouraged
- **Commits**: Conventional commits (`feat:`, `fix:`, `docs:`, etc.)

## Pull Request Process

1. Ensure CI passes on both Linux and Windows
2. Update docs if adding features
3. Keep changes focused and reviewable
