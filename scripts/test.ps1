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

Write-Host "[INFO] Unit tests passed" -ForegroundColor Green
Write-Host ""

# Run Python integration suite
Write-Host "[INFO] Running integration tests..." -ForegroundColor Green
$integrationArgs = @("$RepoRoot\tests\integration\test_all.py")
if ($Verbose) {
    $integrationArgs += "--verbose"
}

python @integrationArgs
$integrationExit = $LASTEXITCODE

if ($integrationExit -ne 0) {
    Write-Host "[ERROR] Integration tests failed." -ForegroundColor Red
    Pop-Location
    exit $integrationExit
}

Write-Host "[INFO] Integration tests passed" -ForegroundColor Green
Write-Host ""

# Run validation scenarios
Write-Host "[INFO] Running validation scenarios..." -ForegroundColor Green
$scenarioArgs = @("$RepoRoot\tests\scenarios\run_scenarios.py")
if ($Verbose) {
    $scenarioArgs += "--verbose"
}

python @scenarioArgs
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    Write-Host "[ERROR] Validation scenarios failed." -ForegroundColor Red
} else {
    Write-Host "[INFO] All tests passed" -ForegroundColor Green
}

Pop-Location
exit $exitCode
