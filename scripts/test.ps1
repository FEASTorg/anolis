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
    Write-Host "[INFO] ThreadSanitizer build detected" -ForegroundColor Green

    $tripletLine = $cacheContent | Where-Object { $_ -match "^VCPKG_TARGET_TRIPLET:STRING=" }
    if (-not $tripletLine) {
        Write-Host "[ERROR] Could not determine VCPKG_TARGET_TRIPLET from CMakeCache.txt" -ForegroundColor Red
        exit 3
    }

    $Triplet = ($tripletLine -split "=")[1]
    Write-Host "[INFO] Using VCPKG triplet: $Triplet" -ForegroundColor Green

    # NOTE: On Windows, CMake should copy required DLLs to the output directory.
    # If tests fail to find TSAN DLLs, check CMake's RUNTIME_OUTPUT_DIRECTORY
    # and vcpkg's automatic DLL copying. Modifying PATH here can cause ctest.exe
    # itself to load TSAN libraries, potentially causing issues.
    
    # Set explicit TSAN environment marker for test detection
    $env:ANOLIS_TSAN = "1"
    
    # Configure TSAN runtime behavior (for test processes)
    $env:TSAN_OPTIONS = "second_deadlock_stack=1 detect_deadlocks=1 history_size=7"
    Write-Host "[INFO] ANOLIS_TSAN=1 (timing-sensitive tests will skip)" -ForegroundColor Green
    Write-Host "[INFO] TSAN_OPTIONS=$env:TSAN_OPTIONS" -ForegroundColor Green

    # Ensure provider also built with TSAN
    $providerCache = Join-Path $ProviderDir "$ProviderBuildDir\CMakeCache.txt"
    if (Test-Path $providerCache) {
        $providerCacheContent = Get-Content $providerCache
        if (-not ($providerCacheContent -match "ENABLE_TSAN:BOOL=ON")) {
            Write-Host "[ERROR] Provider built without TSAN while runtime uses TSAN." -ForegroundColor Red
            Write-Host "        Rebuild provider with -TSan to avoid mixed instrumentation." -ForegroundColor Red
            exit 4
        }
    }
}

# ------------------------------------------------------------------------------
# Run C++ Unit Tests
# ------------------------------------------------------------------------------

Write-Host "[INFO] Discovering unit tests..."

Push-Location $BuildDir
$testList = & ctest -N -C $Configuration
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] ctest discovery failed." -ForegroundColor Red
    exit $LASTEXITCODE
}
Pop-Location

$match = $testList | Select-String "Total Tests:"
if (-not $match) {
    Write-Host "[ERROR] No unit tests found. Ensure BUILD_TESTING=ON." -ForegroundColor Red
    exit 2
}

$TestCount = ($match.Line -replace "Total Tests:\s*", "")
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
    # Find runtime executable in the build directory
    $runtimeCandidates = @(
        (Join-Path $BuildDir "core\$Configuration\anolis-runtime.exe"),
        (Join-Path $BuildDir "core\anolis-runtime.exe"),
        (Join-Path $BuildDir "core\$Configuration\anolis-runtime"),
        (Join-Path $BuildDir "core\anolis-runtime")
    )
    
    $runtimePath = $null
    foreach ($candidate in $runtimeCandidates) {
        if (Test-Path $candidate) {
            $runtimePath = $candidate
            break
        }
    }
    
    if (-not $runtimePath) {
        Write-Host "[ERROR] Runtime executable not found in $BuildDir\core\" -ForegroundColor Red
        Write-Host "[ERROR] Expected: anolis-runtime.exe or anolis-runtime" -ForegroundColor Red
        exit 5
    }
    
    # Find provider executable (optional)
    $providerArgs = @()
    $providerCandidates = @(
        (Join-Path $ProviderDir "$ProviderBuildDir\$Configuration\anolis-provider-sim.exe"),
        (Join-Path $ProviderDir "$ProviderBuildDir\anolis-provider-sim.exe"),
        (Join-Path $ProviderDir "$ProviderBuildDir\$Configuration\anolis-provider-sim"),
        (Join-Path $ProviderDir "$ProviderBuildDir\anolis-provider-sim")
    )
    
    $providerPath = $null
    foreach ($candidate in $providerCandidates) {
        if (Test-Path $candidate) {
            $providerPath = $candidate
            break
        }
    }
    
    Write-Host "[INFO] Runtime: $runtimePath" -ForegroundColor Green
    if ($providerPath) {
        Write-Host "[INFO] Provider: $providerPath" -ForegroundColor Green
        $providerArgs = @("--provider", $providerPath)
    }
    
    python $integrationScript --runtime $runtimePath @providerArgs
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
    # Reuse runtime and provider paths from integration tests
    $scenarioArgs = @()
    if ($runtimePath) {
        $scenarioArgs += @("--runtime", $runtimePath)
    }
    if ($providerPath) {
        $scenarioArgs += @("--provider", $providerPath)
    }
    
    python $scenarioScript @scenarioArgs
    $scenarioExit = $LASTEXITCODE
}

if ($scenarioExit -ne 0) {
    Write-Host "[ERROR] Validation scenarios failed." -ForegroundColor Red
    exit $scenarioExit
}

Write-Host "[INFO] All tests passed" -ForegroundColor Green
Pop-Location
exit 0
