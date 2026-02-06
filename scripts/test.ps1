# Anolis Test Script (Windows)
#
# Usage:
#   .\scripts\test.ps1 [-Verbose] [-Configuration Release]

param(
    [switch]$Verbose,
    [string]$Configuration = "Release"
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $RepoRoot "build"

Write-Host "[INFO] Running Anolis test suite..." -ForegroundColor Green
Push-Location $RepoRoot

# Run C++ unit tests via CTest (build must already exist)
if (-not (Test-Path (Join-Path $BuildDir "CTestTestfile.cmake"))) {
    Write-Host "[ERROR] Build directory missing (expected $BuildDir). Please configure & build before running tests." -ForegroundColor Red
    exit 2
}

$ctestArgs = @("--output-on-failure", "-C", $Configuration)
if ($Verbose) {
    $ctestArgs += "-VV"
}

Push-Location $BuildDir
& ctest @ctestArgs
$unitExit = $LASTEXITCODE
Pop-Location

if ($unitExit -ne 0) {
    Write-Host "[ERROR] Unit tests failed." -ForegroundColor Red
    Pop-Location
    exit $unitExit
}

# Run Python integration suite
$args = @("$RepoRoot\tests\integration\test_all.py")
if ($Verbose) {
    $args += "--verbose"
}

python @args
$exitCode = $LASTEXITCODE

Pop-Location
exit $exitCode
