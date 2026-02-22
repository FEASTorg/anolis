# Scripts

Preset-first script reference for `anolis`.

## Core Scripts

| Script           | Purpose                                      | Default Preset |
| ---------------- | -------------------------------------------- | -------------- |
| `build.ps1/.sh`  | Configure + build Anolis                     | Platform default (`dev-release` Linux/macOS, `dev-windows-release` Windows) |
| `test.ps1/.sh`   | Run CTest from preset                        | Platform default (`dev-release` Linux/macOS, `dev-windows-release` Windows) |
| `run.ps1/.sh`    | Launch runtime from a preset build           | Platform default (`dev-release` Linux/macOS, `dev-windows-release` Windows) |
| `setup.ps1/.sh`  | Validate local deps + install Python lock + preset build | Platform default (`dev-release` Linux/macOS, `dev-windows-release` Windows) |
| `dev.ps1/.sh`    | Developer convenience wrapper (`build` then `test`) | Platform default (`dev-release` Linux/macOS, `dev-windows-release` Windows) |

Discover available presets:

```bash
cmake --list-presets
ctest --list-presets
```

## Build

```powershell
# Windows
.\scripts\build.ps1 -Preset dev-windows-release
.\scripts\build.ps1 -Preset dev-windows-debug -Clean

# Linux/macOS
bash ./scripts/build.sh --preset dev-release
bash ./scripts/build.sh --preset dev-debug --clean
```

### Useful Presets

- `dev-debug`
- `dev-release`
- `dev-windows-debug`
- `dev-windows-release`
- `ci-linux-release`
- `ci-windows-release`
- `ci-coverage`
- `ci-asan`
- `ci-ubsan`
- `ci-tsan`
- `ci-linux-compat`

## Test

```powershell
# Windows
.\scripts\test.ps1 -Preset dev-windows-release
.\scripts\test.ps1 -Preset dev-windows-release -Label unit

# Linux/macOS
bash ./scripts/test.sh --preset ci-linux-release
bash ./scripts/test.sh --preset ci-linux-release --label integration
```

## Run / Setup Examples

```powershell
# Windows
.\scripts\setup.ps1 -Preset dev-windows-release
.\scripts\run.ps1 -Preset dev-windows-release

# Linux/macOS
bash ./scripts/setup.sh --preset dev-release
bash ./scripts/run.sh --preset dev-release
```

Notes:
- `test.*` is CTest-first.
- Python integration registration is opt-in at configure time (`ANOLIS_ENABLE_PYTHON_CTEST`).
- `setup.*` requires `VCPKG_ROOT` to already point to an installed vcpkg toolchain.
- On Windows, use `dev-windows-*` presets for local work; `dev-*` presets are Ninja-based and intended for Linux/macOS.

## Policy Notes

- `anolis` scripts no longer build sibling repositories by default.
- Cross-repo runtime/provider validation is handled in the pinned compatibility CI lane (`anolis-provider-compat`).
