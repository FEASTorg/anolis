# Mixed-Bus Config Pack

This directory contains canonical runtime/provider config sets for running `bread0` + `ezo0` together.

## Runtime Profiles

1. `anolis-runtime.mixed.win.mock.yaml`
   - Windows local validation without EZO hardware.
   - Uses `provider-bread.mock.yaml` + `provider-ezo.mock.yaml`.
   - HTTP port `18080`.
   - Expected inventory: 6 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`, `ec0`).

2. `anolis-runtime.mixed.yaml`
   - Linux real-hardware validation profile.
   - Uses `provider-bread.yaml` + `provider-ezo.yaml`.
   - Address map aligns with CRUMBS `mixed_bus_lab_validation`: RLHT `0x0A`, DCMT `0x14`, DCMT `0x15`, EZO pH `0x63`, EZO DO `0x61`.
   - Bosch `0x76`/`0x77` checks from CRUMBS are optional and intentionally outside provider mixed-bus scope.
   - Requires `anolis-provider-bread` built with `dev-linux-hardware-release`.
   - Requires `anolis-provider-ezo` built with `dev-linux-hardware-release`.
   - `provider-bread.yaml` sets `hardware.require_live_session: true` to fail fast on non-hardware bread builds.
   - Polling interval is tuned to `2500ms` to avoid state-cache overruns with serialized EZO reads.
   - HTTP port `8080`.
   - Expected inventory: 5 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`).

## Provider Configs

1. `provider-bread.yaml` (BREAD Linux hardware profile, requires live session)
2. `provider-bread.mock.yaml` (BREAD Windows mock profile)
3. `provider-ezo.yaml` (EZO Linux hardware profile: `ph0`, `do0`)
4. `provider-ezo.mock.yaml` (EZO Windows mock profile: `ph0`, `do0`, `ec0`)

## Run Commands

Use these exact commands.

## 0) Build Prerequisites (Preset-Based)

Build all three repos before validation so executable paths in the runtime configs exist.

### Linux/macOS

```bash
cd /path/to/anolis
cmake --preset dev-release
cmake --build --preset dev-release

cd /path/to/anolis-provider-bread
cmake --preset dev-linux-hardware-release
cmake --build --preset dev-linux-hardware-release

cd /path/to/anolis-provider-ezo
cmake --preset dev-linux-hardware-release
cmake --build --preset dev-linux-hardware-release
```

Note: in `anolis-provider-ezo`, `dev-linux-hardware-*` are cross-provider naming aliases.

### Windows (PowerShell)

```powershell
Set-Location D:\repos_feast\anolis
cmake --preset dev-windows-release
cmake --build --preset dev-windows-release

Set-Location D:\repos_feast\anolis-provider-bread
cmake --preset dev-windows-release
cmake --build --preset dev-windows-release

Set-Location D:\repos_feast\anolis-provider-ezo
cmake --preset dev-windows-release
cmake --build --preset dev-windows-release
```

## A) Windows Mock Validation (PowerShell)

### 1) Start runtime (Terminal A)

```powershell
Set-Location D:\repos_feast\anolis
Get-NetTCPConnection -LocalPort 18080 -ErrorAction SilentlyContinue
.\build\dev-windows-release\core\Release\anolis-runtime.exe --config ..\anolis-provider-ezo\config\mixed-bus\anolis-runtime.mixed.win.mock.yaml
```

### 2) Validate endpoints (Terminal B)

```powershell
$base = 'http://127.0.0.1:18080'
Invoke-RestMethod "$base/v0/runtime/status" | ConvertTo-Json -Depth 8
Invoke-RestMethod "$base/v0/providers/health" | ConvertTo-Json -Depth 8
Invoke-RestMethod "$base/v0/devices" | ConvertTo-Json -Depth 8
Invoke-RestMethod "$base/v0/state" | ConvertTo-Json -Depth 8
```

Expected:

1. Runtime stays up.
2. `bread0` and `ezo0` are present.
3. Inventory includes 6 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`, `ec0`).

Note:

1. On Windows mock path, `bread0` may log `no hardware session`; that is expected.

## B) Linux Hardware Validation

This profile is aligned to the current lab hardware map:
RLHT `0x0A`, DCMT `0x14`, DCMT `0x15`, EZO pH `0x63`, EZO DO `0x61`.

### 1) Start runtime (Terminal A)

```bash
cd /path/to/anolis
./build/dev-release/core/anolis-runtime --config ../anolis-provider-ezo/config/mixed-bus/anolis-runtime.mixed.yaml
```

### 2) Validate endpoints (Terminal B)

```bash
cd /path/to/anolis-provider-ezo
./scripts/mixed-bus/check_mixed_bus_http.sh \
  --base-url http://127.0.0.1:8080 \
  --expect-providers bread0,ezo0 \
  --min-device-count 5 \
  --capture-dir artifacts/mixed-bus-validation/mixed
```

Expected:

1. Runtime and both providers are `AVAILABLE`.
2. Inventory includes 5 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`).
3. Script exits `0` and writes artifacts.
4. Repeated `Poll took longer than interval` warnings should not appear with `interval_ms: 2500`.

If `bread0` fails startup with
`hardware.require_live_session=true but provider was built without hardware support`,
rebuild `anolis-provider-bread` with `dev-linux-hardware-release` and rerun.

If runtime fails to spawn `ezo0` due to missing binary path,
rebuild `anolis-provider-ezo` with `dev-linux-hardware-release`.

If `ezo0` hello times out during startup, the Linux profile uses `hello_timeout_ms: 5000`
to absorb normal sensor startup latency.
