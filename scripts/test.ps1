# Anolis Test Script (Windows)
#
# Usage:
#   .\scripts\test.ps1 [-Verbose]

param(
    [switch]$Verbose
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

Write-Host "[INFO] Running Anolis test suite..." -ForegroundColor Green
Push-Location $RepoRoot

$args = @("$ScriptDir\test_all.py")
if ($Verbose) {
    $args += "--verbose"
}

python @args
$exitCode = $LASTEXITCODE

Pop-Location
exit $exitCode
