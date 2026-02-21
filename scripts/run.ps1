# Anolis Runtime Start Script (Windows)
#
# Usage:
#   .\scripts\run.ps1 [-Preset NAME] [-Config PATH] [-BuildDir PATH]

param(
    [string]$Preset = "",
    [string]$Config,
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"

function Get-DefaultPreset {
    if ($env:OS -eq "Windows_NT") {
        return "dev-windows-release"
    }
    return "dev-release"
}

if (-not $Preset) {
    $Preset = Get-DefaultPreset
}
if (($env:OS -eq "Windows_NT") -and $Preset -in @("dev-release", "dev-debug")) {
    throw "Preset '$Preset' uses Ninja and may select MinGW on Windows. Use 'dev-windows-release', 'dev-windows-debug', or 'ci-windows-release'."
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build\$Preset"
}

if (-not $Config) {
    $Config = Join-Path $RepoRoot "anolis-runtime.yaml"
}

if (-not (Test-Path $Config)) {
    Write-Host "[ERROR] Config file not found: $Config" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $BuildDir)) {
    Write-Host "[ERROR] Build directory not found: $BuildDir" -ForegroundColor Red
    Write-Host "Run: .\scripts\build.ps1 -Preset $Preset" -ForegroundColor Yellow
    exit 1
}

$candidates = @(
    (Join-Path $BuildDir "core\anolis-runtime.exe"),
    (Join-Path $BuildDir "core\Release\anolis-runtime.exe"),
    (Join-Path $BuildDir "core\Debug\anolis-runtime.exe")
)

$Runtime = $null
foreach ($candidate in $candidates) {
    if (Test-Path $candidate) {
        $Runtime = $candidate
        break
    }
}

if (-not $Runtime) {
    Write-Host "[ERROR] Runtime executable not found under: $BuildDir\core" -ForegroundColor Red
    Write-Host "Run: .\scripts\build.ps1 -Preset $Preset" -ForegroundColor Yellow
    exit 1
}

$CMakeCache = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $CMakeCache) {
    $cacheContent = Get-Content $CMakeCache -Raw
    if ($cacheContent -match "ENABLE_TSAN:BOOL=ON") {
        Write-Host "[INFO] ThreadSanitizer build detected" -ForegroundColor Yellow
        Write-Host "[WARN] TSAN is primarily supported on Linux (Clang/GCC)." -ForegroundColor Yellow
    }
}

Write-Host "[INFO] Starting Anolis Runtime..." -ForegroundColor Green
Write-Host "[INFO] Preset:          $Preset" -ForegroundColor Green
Write-Host "[INFO] Build directory: $BuildDir" -ForegroundColor Green
Write-Host "[INFO] Executable:      $Runtime" -ForegroundColor Green
Write-Host "[INFO] Config:          $Config" -ForegroundColor Green
Write-Host ""

& $Runtime "--config=$Config"
exit $LASTEXITCODE
