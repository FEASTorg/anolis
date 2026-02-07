# Contributing to Anolis

Thank you for your interest in contributing to Anolis!

## Getting Started

### Prerequisites

- **Windows**: Visual Studio 2022 with C++ desktop development workload
- **Linux**: GCC 11+, CMake 3.20+
- **Git**: For version control

### Setup

1. **Repository Layout**:
   The build scripts assume `anolis` and `anolis-provider-sim` are sibling directories.
   **You must clone them into the same parent folder.**

   ```text
   workspace/
   ├── anolis/
   └── anolis-provider-sim/
   ```

2. Clone the repositories:

   ```bash
   git clone https://github.com/FEASTorg/anolis.git
   git clone https://github.com/FEASTorg/anolis-provider-sim.git
   ```

3. Run the setup script:

   ```bash
   # Linux/macOS
   ./scripts/setup.sh

   # Windows (PowerShell)
   .\scripts\setup.ps1
   ```

4. Run the tests:

   ```bash
   # Linux/macOS
   ./scripts/test.sh

   # Windows
   .\scripts\test.ps1
   ```

## Development Workflow

### Building

```bash
# Linux/macOS
./scripts/build.sh

# Windows
.\scripts\build.ps1

# Clean build
./scripts/build.sh --clean
```

### Running

```bash
# Linux/macOS
./scripts/run.sh

# Windows
.\scripts\run.ps1
```

### Testing

```bash
# Run all tests (includes unit + integration)
./scripts/test.sh          # Linux/macOS
.\scripts\test.ps1         # Windows

# Or run integration tests directly
python tests/integration/test_all.py --timeout=60

# With explicit paths
python tests/integration/test_all.py \
  --runtime=build/core/Release/anolis-runtime.exe \
  --provider=../anolis-provider-sim/build/Release/anolis-provider-sim.exe

# Run validation scenarios
python tests/scenarios/run_scenarios.py
```

## Code Guidelines

### C++ Style

- C++17 standard
- Use `std::filesystem` for path handling
- Platform-specific code must be guarded with `#ifdef _WIN32` / `#else`
- Prefer RAII for resource management
- Use `std::unique_ptr` / `std::shared_ptr` appropriately

### Commits

- Write clear, concise commit messages
- Reference issue numbers when applicable
- Keep commits focused on a single change

### Pull Requests

1. Create a branch from `main`
2. Make your changes
3. Ensure all tests pass locally
4. Push and create a PR
5. Wait for CI to pass
6. Request review

## CI/CD

Both `anolis` and `anolis-provider-sim` use GitHub Actions for CI. Tests run on:

- Ubuntu 22.04 (GCC)
- Windows Server 2022 (MSVC 2022)

### Test Requirements

#### Regression Rule

Any bug fixed must include a test that would have caught it.

#### Test Structure

- Unit tests: C++ with GoogleTest in `tests/unit/`
- Integration tests: Python in `tests/integration/`
- Validation scenarios: Python in `tests/scenarios/`
- Tests must clean up processes after completion
- Tests must exit with proper exit codes (0 = pass, non-zero = fail)

## Code Quality

### C++ Code Quality

#### Static Analysis (clang-tidy)

**Primary Check**: clang-tidy runs in CI on **Linux** builds. MSVC/Visual Studio integration is unreliable (mixed diagnostics, incomplete coverage).
So we do **not** enforce clang-tidy on Windows.

**Local runs (recommended: Linux/WSL)**:

- Enable via the build script: `./scripts/build.sh --clang-tidy` (default on Linux; disable with `--no-clang-tidy` if you want a faster build).
- You need `clang-tidy` installed (`sudo apt install clang-tidy`).

**Windows note**: You can install clang-tidy with the "C++ Clang tools" workload or LLVM, but results under MSVC are incomplete.
Prefer running analysis from Linux/WSL instead.

**To apply fixes**: Use Linux/WSL with `compile_commands.json`; MSVC generators do not support apply-fixes reliably.

#### Formatting (clang-format)

We use `clang-format` to enforce C++ style.

```bash
# Apply formatting
clang-format -i core/runtime/src/main.cpp

# Recursively (PowerShell)
Get-ChildItem -Path core -Recurse -Include *.cpp,*.hpp | ForEach-Object { clang-format -i $_.FullName }

# Recursively (Bash)
find core \( -name "*.cpp" -o -name "*.hpp" \) -print0 | xargs -0 clang-format -i
```

### Python Code Quality

We use `ruff` for both linting and formatting Python scripts.

#### Python setup

```bash
pip install -r requirements.txt
```

#### Generating/updating the lock file (UTF-8)

On Windows PowerShell, redirection (`>`) defaults to UTF-16; regenerate the lock file with UTF-8 to avoid CI decode errors.

```powershell
# PowerShell (PowerShell 7+)
pip freeze > requirements-lock.txt -Encoding utf8

# PowerShell (works across versions)
pip freeze | Out-File -Encoding utf8 requirements-lock.txt

# Git Bash / WSL
pip freeze > requirements-lock.txt

# Quick verify (fails non-zero if not UTF-8)
python -c "open('requirements-lock.txt','rb').read().decode('utf-8')"
```

#### Linting & Formatting

```bash
# Fix auto-fixable lint issues
ruff check --fix .

# Apply formatting
ruff format .
```

### Sanitizers (ASAN)

To run with AddressSanitizer (ASAN) on Linux/macOS:

```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address"
cmake --build build_asan
ctest --test-dir build_asan
```

## Project Structure

```sh
anolis/
├── core/               # Runtime kernel
│   ├── control/        # Call router
│   ├── http/           # HTTP server
│   ├── provider/       # Provider host
│   ├── registry/       # Device registry
│   ├── runtime/        # Config and bootstrap
│   ├── src/            # Main entry point
│   └── state/          # State cache
├── docs/               # Documentation
├── scripts/            # Build and test scripts
├── sdk/                # Language SDKs
├── spec/               # Protocol specifications
├── tools/              # Developer tools
│   └── operator-ui/    # Web-based operator interface
└── working/            # Planning documents
```

---

## Cross-Platform Notes

- **Linux is the reference platform** (deployment target)
- **Windows is a supported dev platform**
- Test on both platforms before submitting changes that touch:
  - Process spawning (`provider_process.cpp`)
  - File paths
  - Signal handling
  - Socket operations

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

### Testing Signal Handling

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

## Getting Help

- Check the [documentation](docs/README.md)
- Review existing issues
- Ask in discussions

## License

By contributing, you agree that your contributions will be licensed under the project's license.
