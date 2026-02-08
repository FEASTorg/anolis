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
# Detect Runtime Executable (single-config friendly)
# ------------------------------------------------------------
$Runtime = Join-Path $BuildDir "core\anolis-runtime.exe"

if (-not (Test-Path $Runtime)) {
    Write-Host "[ERROR] Runtime executable not found at:" -ForegroundColor Red
    Write-Host "  $Runtime" -ForegroundColor Red
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
