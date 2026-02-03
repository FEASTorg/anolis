# Anolis Runtime Start Script (Windows)
#
# Usage:
#   .\scripts\run.ps1 [-Config PATH]

param(
    [string]$Config
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

# Default config
if (-not $Config) {
    $Config = Join-Path $RepoRoot "anolis-runtime.yaml"
}

# Find runtime executable
$RuntimePaths = @(
    "$RepoRoot\build\core\Release\anolis-runtime.exe",
    "$RepoRoot\build\core\Debug\anolis-runtime.exe",
    "$RepoRoot\build\core\anolis-runtime.exe"
)

$Runtime = $null
foreach ($path in $RuntimePaths) {
    if (Test-Path $path) {
        $Runtime = $path
        break
    }
}

if (-not $Runtime) {
    Write-Host "[ERROR] anolis-runtime.exe not found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

Write-Host "[INFO] Starting Anolis Runtime..." -ForegroundColor Green
Write-Host "[INFO] Executable: $Runtime" -ForegroundColor Green
Write-Host "[INFO] Config: $Config" -ForegroundColor Green
Write-Host ""

& $Runtime --config="$Config"
