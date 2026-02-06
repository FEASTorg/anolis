# Anolis Build Script (Windows)
#
# Usage:
#   .\scripts\build.ps1 [-Clean] [-Debug] [-NoTests]

param(
    [switch]$Clean,
    [switch]$Debug,
    [switch]$NoTests
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$ProviderSimDir = Join-Path (Split-Path -Parent $RepoRoot) "anolis-provider-sim"

$BuildType = if ($Debug) { "Debug" } else { "Release" }
$BuildTests = -not $NoTests

Write-Host "[INFO] Build type: $BuildType" -ForegroundColor Green
Write-Host "[INFO] Build tests: $BuildTests" -ForegroundColor Green

# Check vcpkg
if (-not $env:VCPKG_ROOT) {
    $vcpkgPaths = @("$env:USERPROFILE\vcpkg", "C:\vcpkg", "C:\src\vcpkg")
    foreach ($path in $vcpkgPaths) {
        if (Test-Path "$path\vcpkg.exe") {
            $env:VCPKG_ROOT = $path
            break
        }
    }
    if (-not $env:VCPKG_ROOT) {
        Write-Host "[ERROR] VCPKG_ROOT not set. Run setup.ps1 first." -ForegroundColor Red
        exit 1
    }
}

# Clean if requested
if ($Clean) {
    Write-Host "[INFO] Cleaning build directories..." -ForegroundColor Green
    if (Test-Path "$RepoRoot\build") { Remove-Item -Recurse -Force "$RepoRoot\build" }
    if (Test-Path "$ProviderSimDir\build") { Remove-Item -Recurse -Force "$ProviderSimDir\build" }
}

# Build provider-sim
if (Test-Path $ProviderSimDir) {
    Write-Host "[INFO] Building anolis-provider-sim..." -ForegroundColor Green
    Push-Location $ProviderSimDir
    cmake -B build -S . `
        -DCMAKE_BUILD_TYPE="$BuildType" `
        -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
    cmake --build build --config $BuildType
    Pop-Location
}

# Build anolis
Write-Host "[INFO] Building anolis..." -ForegroundColor Green
Push-Location $RepoRoot
cmake -B build -S . `
    -DCMAKE_BUILD_TYPE="$BuildType" `
    -DBUILD_TESTING=$(if ($BuildTests) { "ON" } else { "OFF" }) `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config $BuildType
Pop-Location

Write-Host "[INFO] Build complete" -ForegroundColor Green
