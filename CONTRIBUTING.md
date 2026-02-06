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
# Run all tests
python scripts/test_all.py

# Run specific test suite
python scripts/test_core.py
python scripts/test_http.py

# Verbose output
python scripts/test_all.py --verbose
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

## Testing Requirements

### Regression Rule

Any bug fixed must include a test that would have caught it.

### Test Structure

- Tests must be runnable standalone (`python scripts/test_core.py`)
- Tests must clean up processes after completion
- Tests must exit with proper exit codes (0 = pass, non-zero = fail)

## Code Quality

### C++ Code Quality

#### Static Analysis (clang-tidy)

**Primary Check**: clang-tidy runs in CI on **Linux** builds. MSVC/Visual Studio integration is unreliable (mixed diagnostics, incomplete coverage), so we do **not** enforce clang-tidy on Windows.

**Local runs (recommended: Linux/WSL)**:

- Enable via the build script: `./scripts/build.sh --clang-tidy` (default on Linux; disable with `--no-clang-tidy` if you want a faster build).
- You need `clang-tidy` installed (`sudo apt install clang-tidy`).

**Windows note**: You can install clang-tidy with the "C++ Clang tools" workload or LLVM, but results under MSVC are incomplete; prefer running analysis from Linux/WSL instead.

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

## Cross-Platform Notes

- **Linux is the reference platform** (deployment target)
- **Windows is a supported dev platform**
- Test on both platforms before submitting changes that touch:
  - Process spawning (`provider_process.cpp`)
  - File paths
  - Signal handling
  - Socket operations

## Getting Help

- Check the [documentation](docs/README.md)
- See [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) for common pitfalls and CI troubleshooting
- Review existing issues
- Ask in discussions

## License

By contributing, you agree that your contributions will be licensed under the project's license.
