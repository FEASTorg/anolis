# Anolis Development Setup Script (Windows)
#
# This script bootstraps a development environment:
# - Checks prerequisites
# - Sets up vcpkg
# - Builds anolis and anolis-provider-sim
#
# Usage:
#   .\scripts\setup.ps1 [-Clean]
#
# Options:
#   -Clean    Remove build directories before building

param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$ProviderSimDir = Join-Path (Split-Path -Parent $RepoRoot) "anolis-provider-sim"

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Error-Exit {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
    exit 1
}

Write-Host "=============================================="
Write-Host "Anolis Development Setup"
Write-Host "=============================================="
Write-Host ""

# Check prerequisites
Write-Info "Checking prerequisites..."

# Check cmake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Error-Exit "cmake not found. Install from https://cmake.org/download/ or via Visual Studio Installer"
}
$cmakeVersion = (cmake --version | Select-Object -First 1)
Write-Info "  cmake: $cmakeVersion"

# Check C++ compiler (cl.exe from Visual Studio)
$cl = Get-Command cl -ErrorAction SilentlyContinue
if (-not $cl) {
    Write-Warn "cl.exe not in PATH. Make sure to run from Developer Command Prompt or VS Code with C++ tools."
    Write-Info "  Checking if Visual Studio is installed..."
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -property installationPath
        Write-Info "  Visual Studio: $vsPath"
    }
    else {
        Write-Error-Exit "Visual Studio not found. Install Visual Studio 2022 with C++ desktop development workload."
    }
}
else {
    Write-Info "  cl.exe: found"
}

# Check python
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command python3 -ErrorAction SilentlyContinue
}
if (-not $python) {
    Write-Error-Exit "python not found. Install from https://python.org or Microsoft Store"
}
$pythonVersion = (python --version 2>&1)
Write-Info "  python: $pythonVersion"

# Check git
$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    Write-Error-Exit "git not found. Install from https://git-scm.com/download/win"
}
$gitVersion = (git --version)
Write-Info "  git: $gitVersion"

# Check/setup vcpkg
if (-not $env:VCPKG_ROOT) {
    # Check common locations
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
        Write-Warn "VCPKG_ROOT not set and vcpkg not found in common locations."
        Write-Info "Installing vcpkg to $env:USERPROFILE\vcpkg..."
        git clone https://github.com/Microsoft/vcpkg.git "$env:USERPROFILE\vcpkg"
        & "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
        $env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"
    }
}

if (-not (Test-Path "$env:VCPKG_ROOT\vcpkg.exe")) {
    Write-Error-Exit "vcpkg.exe not found at $env:VCPKG_ROOT\vcpkg.exe"
}
Write-Info "  vcpkg: $env:VCPKG_ROOT"

# Check pip packages
Write-Info "Checking Python packages..."
python -m pip install --quiet --upgrade pip
python -m pip install --quiet requests
Write-Info "  requests: installed"

Write-Host ""

# Initialize submodules
Write-Info "Initializing git submodules..."
Push-Location $RepoRoot
git submodule update --init --recursive
Pop-Location
Write-Info "  Submodules initialized"

Write-Host ""

# Check for provider-sim repo
if (-not (Test-Path $ProviderSimDir)) {
    Write-Warn "anolis-provider-sim not found at $ProviderSimDir"
    Write-Info "Cloning anolis-provider-sim..."
    git clone https://github.com/FEASTorg/anolis-provider-sim.git $ProviderSimDir
}

# Clean if requested
if ($Clean) {
    Write-Info "Cleaning build directories..."
    if (Test-Path "$RepoRoot\build") {
        Remove-Item -Recurse -Force "$RepoRoot\build"
    }
    if (Test-Path "$ProviderSimDir\build") {
        Remove-Item -Recurse -Force "$ProviderSimDir\build"
    }
    Write-Info "  Cleaned"
}

Write-Host ""

# Build anolis-provider-sim
Write-Info "Building anolis-provider-sim..."
Push-Location $ProviderSimDir

if (-not (Test-Path "build")) {
    Write-Info "  Configuring..."
    cmake -B build -S . `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
}

Write-Info "  Compiling..."
cmake --build build --config Release

$providerExe = "build\Release\anolis-provider-sim.exe"
if (Test-Path $providerExe) {
    Write-Info "  anolis-provider-sim built successfully"
}
else {
    Pop-Location
    Write-Error-Exit "Failed to build anolis-provider-sim"
}

Pop-Location

Write-Host ""

# Build anolis
Write-Info "Building anolis..."
Push-Location $RepoRoot

if (-not (Test-Path "build")) {
    Write-Info "  Configuring..."
    cmake -B build -S . `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
}

Write-Info "  Compiling..."
cmake --build build --config Release

$runtimeExe = "build\core\Release\anolis-runtime.exe"
if (Test-Path $runtimeExe) {
    Write-Info "  anolis-runtime built successfully"
}
else {
    Pop-Location
    Write-Error-Exit "Failed to build anolis-runtime"
}

Pop-Location

Write-Host ""
Write-Host "=============================================="
Write-Host "Setup Complete!"
Write-Host "=============================================="
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Run tests:     .\scripts\test.ps1"
Write-Host "  2. Start runtime: .\scripts\run.ps1"
Write-Host ""
