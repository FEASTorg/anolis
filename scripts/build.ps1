# Anolis Build Script (Windows)
#
# Usage:
#   .\scripts\build.ps1 [-Clean] [-Debug] [-NoTests] [-TSan] [-WithFluxGraph] [-FluxGraphDir <path>]

param(
    [switch]$Clean,
    [switch]$Debug,
    [switch]$NoTests,
    [switch]$TSan,
    [switch]$WithFluxGraph,
    [string]$FluxGraphDir
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$ProviderSimDir = Join-Path (Split-Path -Parent $RepoRoot) "anolis-provider-sim"

$BuildType = if ($Debug) { "Debug" } else { "Release" }
$BuildTests = -not $NoTests
$BuildTestingValue = if ($BuildTests) { "ON" } else { "OFF" }

# ---- ThreadSanitizer (not supported on MSVC) ----
if ($TSan) {
    Write-Host "[ERROR] ThreadSanitizer is not supported on Windows/MSVC." -ForegroundColor Red
    Write-Host "        Use WSL or Linux for TSAN builds." -ForegroundColor Yellow
    exit 1
}

# ---- Build directories (match Linux structure philosophy) ----
$BuildDir = Join-Path $RepoRoot "build"
$ProviderBuildDir = Join-Path $ProviderSimDir "build"

# ---- Generator selection ----
$CMakeGeneratorArgs = @()
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
$vsInstance = $null

if (Test-Path $vswhere) {
    $vsInstance = & $vswhere -latest -products * `
        -requires Microsoft.Component.MSBuild `
        -property installationPath
}

if ($vsInstance) {
    Write-Host "[INFO] Using Visual Studio 17 2022 at $vsInstance" -ForegroundColor Green
    $CMakeGeneratorArgs += @(
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_GENERATOR_INSTANCE=$vsInstance"
    )
}
elseif (Get-Command ninja -ErrorAction SilentlyContinue) {
    Write-Host "[INFO] Using Ninja generator" -ForegroundColor Green
    $CMakeGeneratorArgs += @("-G", "Ninja")
}
else {
    Write-Host "[ERROR] No suitable CMake generator found. Install VS 2022 Build Tools or Ninja." -ForegroundColor Red
    exit 1
}

Write-Host "[INFO] Build type: $BuildType" -ForegroundColor Green
Write-Host "[INFO] Build tests: $BuildTests" -ForegroundColor Green
Write-Host "[INFO] Provider FluxGraph: $(if ($WithFluxGraph) { 'ON' } else { 'OFF' })" -ForegroundColor Green
if ($WithFluxGraph -and $FluxGraphDir) {
    Write-Host "[INFO] Provider FluxGraph dir: $FluxGraphDir" -ForegroundColor Green
}
elseif ((-not $WithFluxGraph) -and $FluxGraphDir) {
    Write-Host "[WARN] FluxGraphDir ignored because provider FluxGraph is disabled." -ForegroundColor Yellow
}

# ---- vcpkg detection ----
if (-not $env:VCPKG_ROOT) {
    $vcpkgPaths = @(
        "$env:USERPROFILE\vcpkg",
        "C:\vcpkg",
        "C:\src\vcpkg"
    )

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

$ToolchainFile = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"

# ---- Clean ----
if ($Clean) {
    Write-Host "[INFO] Cleaning build directories..." -ForegroundColor Green
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
    if (Test-Path $ProviderBuildDir) { Remove-Item -Recurse -Force $ProviderBuildDir }
}

# ---- Common CMake arguments ----
$CommonArgs = @(
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
)

$AnolisArgs = @(
    $CommonArgs
    "-DBUILD_TESTING=$BuildTestingValue"
)

$ProviderArgs = @(
    $CommonArgs
    "-DENABLE_FLUXGRAPH=$(if ($WithFluxGraph) { 'ON' } else { 'OFF' })"
)
if ($WithFluxGraph -and $FluxGraphDir) {
    $ProviderArgs += "-DFLUXGRAPH_DIR=$FluxGraphDir"
}

# ---- Build provider-sim ----
if (Test-Path $ProviderSimDir) {
    Write-Host "[INFO] Building anolis-provider-sim..." -ForegroundColor Green
    cmake @CMakeGeneratorArgs -B $ProviderBuildDir -S $ProviderSimDir @ProviderArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] anolis-provider-sim CMake configure failed." -ForegroundColor Red
        exit $LASTEXITCODE
    }

    cmake --build $ProviderBuildDir --config $BuildType
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] anolis-provider-sim build failed." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}
else {
    Write-Host "[INFO] anolis-provider-sim not found (skipping)" -ForegroundColor Yellow
}

# ---- Build anolis ----
Write-Host "[INFO] Building anolis..." -ForegroundColor Green
cmake @CMakeGeneratorArgs -B $BuildDir -S $RepoRoot @AnolisArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] anolis CMake configure failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

cmake --build $BuildDir --config $BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] anolis build failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[INFO] Build complete" -ForegroundColor Green
Write-Host "[INFO] Build directory: $BuildDir" -ForegroundColor Green
if (Test-Path $ProviderSimDir) {
    Write-Host "[INFO] Provider build directory: $ProviderBuildDir" -ForegroundColor Green
}
