# Anolis Test Script (Windows)
#
# Usage:
#   .\scripts\test.ps1 [-Verbose] [-Configuration Release] [-TSan] [-BuildDir PATH]

param(
    [switch]$Verbose,
    [string]$Configuration,
    [switch]$TSan,
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

if (-not $BuildDir) {
    if ($TSan) {
        $BuildDir = Join-Path $RepoRoot "build-tsan"
        $ProviderBuildDir = "build-tsan"
        Write-Host "[INFO] Using TSan build directories" -ForegroundColor Green
    }
    else {
        $BuildDir = Join-Path $RepoRoot "build"
        $ProviderBuildDir = "build"
    }
}
else {
    $ProviderBuildDir = "build"
}

$ProviderDir = Join-Path (Split-Path -Parent $RepoRoot) "anolis-provider-sim"

Write-Host "[INFO] Running Anolis test suite..." -ForegroundColor Green
Push-Location $RepoRoot

# ------------------------------------------------------------------------------
# Validate build directory
# ------------------------------------------------------------------------------

if (-not (Test-Path (Join-Path $BuildDir "CTestTestfile.cmake"))) {
    Write-Host "[ERROR] Build directory missing or not configured: $BuildDir" -ForegroundColor Red
    Write-Host "        Run .\scripts\build.ps1 first."
    exit 2
}

# ------------------------------------------------------------------------------
# Detect configuration (for multi-config generators)
# ------------------------------------------------------------------------------

if (-not $Configuration) {
    $cache = Get-Content (Join-Path $BuildDir "CMakeCache.txt") -ErrorAction SilentlyContinue
    $configLine = $cache | Where-Object { $_ -match "^CMAKE_BUILD_TYPE:" }
    if ($configLine) {
        $Configuration = ($configLine -split "=")[1]
        Write-Host "[INFO] Auto-detected configuration: $Configuration"
    }
    else {
        $Configuration = "Release"
        Write-Host "[INFO] Defaulting to Release configuration"
    }
}

# ------------------------------------------------------------------------------
# Detect TSAN
# ------------------------------------------------------------------------------

$cacheContent = Get-Content (Join-Path $BuildDir "CMakeCache.txt")

if ($cacheContent -match "ENABLE_TSAN:BOOL=ON") {
    Write-Host "[INFO] ThreadSanitizer build detected"

    $tripletLine = $cacheContent | Where-Object { $_ -match "^VCPKG_TARGET_TRIPLET:STRING=" }
    if (-not $tripletLine) {
        Write-Host "[ERROR] Could not determine VCPKG_TARGET_TRIPLET from CMakeCache.txt" -ForegroundColor Red
        exit 3
    }

    $Triplet = ($tripletLine -split "=")[1]
    Write-Host "[INFO] Using VCPKG triplet: $Triplet"

    $VcpkgLib = Join-Path $BuildDir "vcpkg_installed\$Triplet\bin"
    $env:PATH = "$VcpkgLib;$env:PATH"
    Write-Host "[INFO] PATH updated with: $VcpkgLib"

    # Ensure provider also built with TSAN
    $providerCache = Join-Path $ProviderDir "$ProviderBuildDir\CMakeCache.txt"
    if (Test-Path $providerCache) {
        $providerCacheContent = Get-Content $providerCache
        if (-not ($providerCacheContent -match "ENABLE_TSAN:BOOL=ON")) {
            Write-Host "[ERROR] Provider built without TSAN while runtime uses TSAN." -ForegroundColor Red
            Write-Host "        Rebuild provider with -TSan to avoid mixed instrumentation."
            exit 4
        }
    }
}

# ------------------------------------------------------------------------------
# Run C++ Unit Tests
# ------------------------------------------------------------------------------

Write-Host "[INFO] Discovering unit tests..."

Push-Location $BuildDir
$testList = & ctest -N -C $Configuration 2>$null
Pop-Location

$match = $testList | Select-String "Total Tests:"
if (-not $match) {
    Write-Host "[ERROR] No unit tests found. Ensure BUILD_TESTING=ON." -ForegroundColor Red
    exit 2
}

$TestCount = ($match -split "\s+")[-1]
Write-Host "[INFO] Found $TestCount unit tests"

Write-Host "[INFO] Running unit tests..."

$ctestArgs = @("--output-on-failure", "-C", $Configuration)
if ($Verbose) { $ctestArgs += "-VV" }

Push-Location $BuildDir
& ctest @ctestArgs
$unitExit = $LASTEXITCODE
Pop-Location

if ($unitExit -ne 0) {
    Write-Host "[ERROR] Unit tests failed." -ForegroundColor Red
    exit $unitExit
}

Write-Host "[INFO] Unit tests passed" -ForegroundColor Green
Write-Host ""

# ------------------------------------------------------------------------------
# Integration Tests
# ------------------------------------------------------------------------------

Write-Host "[INFO] Running integration tests..." -ForegroundColor Green

$integrationScript = Join-Path $RepoRoot "tests\integration\test_all.py"
if (-not (Test-Path $integrationScript)) {
    Write-Host "[WARN] Integration test script not found: $integrationScript" -ForegroundColor Yellow
    Write-Host "[WARN] Skipping integration tests" -ForegroundColor Yellow
    $integrationExit = 0
}
else {
    python $integrationScript
    $integrationExit = $LASTEXITCODE
}

if ($integrationExit -ne 0) {
    Write-Host "[ERROR] Integration tests failed." -ForegroundColor Red
    exit $integrationExit
}

Write-Host "[INFO] Integration tests passed" -ForegroundColor Green
Write-Host ""

# ------------------------------------------------------------------------------
# Validation Scenarios
# ------------------------------------------------------------------------------

Write-Host "[INFO] Running validation scenarios..." -ForegroundColor Green

$scenarioScript = Join-Path $RepoRoot "tests\scenarios\run_scenarios.py"
if (-not (Test-Path $scenarioScript)) {
    Write-Host "[WARN] Validation scenario script not found: $scenarioScript" -ForegroundColor Yellow
    Write-Host "[WARN] Skipping validation scenarios" -ForegroundColor Yellow
    $scenarioExit = 0
}
else {
    python $scenarioScript
    $scenarioExit = $LASTEXITCODE
}

if ($scenarioExit -ne 0) {
    Write-Host "[ERROR] Validation scenarios failed." -ForegroundColor Red
    exit $scenarioExit
}

Write-Host "[INFO] All tests passed" -ForegroundColor Green
Pop-Location
exit 0
