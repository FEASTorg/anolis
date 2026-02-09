# Anolis Runtime Start Script (Windows)
#
# Usage:
#   .\scripts\run.ps1 [-Config PATH] [-BuildDir PATH]

param(
    [string]$Config,
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}

if (-not $Config) {
    $Config = Join-Path $RepoRoot "anolis-runtime.yaml"
}

# ------------------------------------------------------------
# Validation
# ------------------------------------------------------------
if (-not (Test-Path $Config)) {
    Write-Host "[ERROR] Config file not found: $Config" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $BuildDir)) {
    Write-Host "[ERROR] Build directory not found: $BuildDir" -ForegroundColor Red
    Write-Host "Run: .\scripts\build.ps1" -ForegroundColor Yellow
    exit 1
}

# ------------------------------------------------------------
# Detect Runtime Executable (multi-config and single-config)
# ------------------------------------------------------------
# Try Release first (Visual Studio multi-config)
$Runtime = Join-Path $BuildDir "core\Release\anolis-runtime.exe"

if (-not (Test-Path $Runtime)) {
    # Try Debug
    $Runtime = Join-Path $BuildDir "core\Debug\anolis-runtime.exe"
}

if (-not (Test-Path $Runtime)) {
    # Try single-config (Ninja, Unix Makefiles)
    $Runtime = Join-Path $BuildDir "core\anolis-runtime.exe"
}

if (-not (Test-Path $Runtime)) {
    Write-Host "[ERROR] Runtime executable not found. Tried:" -ForegroundColor Red
    Write-Host "  $BuildDir\core\Release\anolis-runtime.exe" -ForegroundColor Red
    Write-Host "  $BuildDir\core\Debug\anolis-runtime.exe" -ForegroundColor Red
    Write-Host "  $BuildDir\core\anolis-runtime.exe" -ForegroundColor Red
    Write-Host "Run: .\scripts\build.ps1" -ForegroundColor Yellow
    exit 1
}

# ------------------------------------------------------------
# Detect TSAN build (informational only on Windows)
# ------------------------------------------------------------
$CMakeCache = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $CMakeCache) {
    $cacheContent = Get-Content $CMakeCache -Raw
    if ($cacheContent -match "ENABLE_TSAN:BOOL=ON") {
        Write-Host "[INFO] ThreadSanitizer build detected" -ForegroundColor Yellow
        Write-Host "[WARN] TSAN is primarily supported on Linux (Clang/GCC)." -ForegroundColor Yellow
    }
}

# ------------------------------------------------------------
# Launch
# ------------------------------------------------------------
Write-Host "[INFO] Starting Anolis Runtime..." -ForegroundColor Green
Write-Host "[INFO] Build directory: $BuildDir" -ForegroundColor Green
Write-Host "[INFO] Executable:      $Runtime" -ForegroundColor Green
Write-Host "[INFO] Config:          $Config" -ForegroundColor Green
Write-Host ""

& $Runtime "--config=$Config"
exit $LASTEXITCODE
