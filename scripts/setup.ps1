# Anolis Development Setup Script (Windows)
#
# Bootstraps development environment:
# - Verifies prerequisites
# - Ensures vcpkg baseline
# - Installs Python dependencies
# - Initializes submodules
# - Optionally cleans
# - Delegates build to build.ps1
#
# Usage:
#   .\scripts\setup.ps1 [-Clean]

param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$ProviderSimDir = Join-Path (Split-Path -Parent $RepoRoot) "anolis-provider-sim"

function Write-Info { param($m) Write-Host "[INFO]  $m" -ForegroundColor Green }
function Write-Warn { param($m) Write-Host "[WARN]  $m" -ForegroundColor Yellow }
function Write-Fail { param($m) Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

function Get-VcpkgBaseline {
    $vcpkgJson = Join-Path $RepoRoot "vcpkg.json"
    if (Test-Path $vcpkgJson) {
        try {
            $json = Get-Content $vcpkgJson | ConvertFrom-Json
            if ($json.'builtin-baseline') {
                return $json.'builtin-baseline'
            }
        }
        catch {}
    }
    Write-Fail "Unable to determine vcpkg builtin-baseline from vcpkg.json"
}

$VcpkgBaseline = Get-VcpkgBaseline

Write-Host "=============================================="
Write-Host "Anolis Development Setup"
Write-Host "=============================================="
Write-Host ""

# -------------------------------------------------
# Prerequisites
# -------------------------------------------------
Write-Info "Checking prerequisites..."

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Fail "cmake not found"
}
Write-Info "  cmake: $(cmake --version | Select-Object -First 1)"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Fail "git not found"
}
Write-Info "  git: $(git --version)"

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    Write-Fail "python not found"
}
Write-Info "  python: $(python --version)"

# Check for Visual Studio toolchain
$cl = Get-Command cl -ErrorAction SilentlyContinue
if (-not $cl) {
    Write-Warn "cl.exe not found in PATH. Use Developer PowerShell or VS Code C++ environment."
}
else {
    Write-Info "  MSVC compiler detected"
}

Write-Host ""

# -------------------------------------------------
# vcpkg Setup
# -------------------------------------------------
if (-not $env:VCPKG_ROOT) {
    $candidate = "$env:USERPROFILE\vcpkg"
    if (Test-Path "$candidate\vcpkg.exe") {
        $env:VCPKG_ROOT = $candidate
    }
    else {
        Write-Info "Cloning vcpkg (baseline: $VcpkgBaseline)"
        git clone https://github.com/microsoft/vcpkg.git $candidate
        $env:VCPKG_ROOT = $candidate
    }
}

if (-not (Test-Path "$env:VCPKG_ROOT\vcpkg.exe")) {
    Write-Info "Bootstrapping vcpkg..."
    Push-Location $env:VCPKG_ROOT
    .\bootstrap-vcpkg.bat
    Pop-Location
}

Push-Location $env:VCPKG_ROOT
$currentCommit = (git rev-parse HEAD).Trim()
if ($currentCommit -ne $VcpkgBaseline) {
    Write-Warn "vcpkg baseline mismatch"
    Write-Warn "  current:  $currentCommit"
    Write-Warn "  expected: $VcpkgBaseline"
    Write-Info "Checking out correct baseline..."
    git fetch
    git checkout $VcpkgBaseline
}
Write-Info "  vcpkg baseline verified"
Pop-Location

Write-Host ""

# -------------------------------------------------
# Python Dependencies
# -------------------------------------------------
Write-Info "Installing Python dependencies..."
python -m pip install --upgrade pip | Out-Null

$lockFile = Join-Path $RepoRoot "requirements-lock.txt"
if (Test-Path $lockFile) {
    python -m pip install -r $lockFile
}
else {
    Write-Warn "requirements-lock.txt not found"
}

Write-Host ""

# -------------------------------------------------
# Git Submodules
# -------------------------------------------------
Write-Info "Initializing submodules..."
Push-Location $RepoRoot
git submodule update --init --recursive
Pop-Location
Write-Info "  Submodules ready"

Write-Host ""

# -------------------------------------------------
# Provider Repository
# -------------------------------------------------
if (-not (Test-Path $ProviderSimDir)) {
    Write-Info "Cloning anolis-provider-sim..."
    git clone https://github.com/FEASTorg/anolis-provider-sim.git $ProviderSimDir
}

# -------------------------------------------------
# Clean
# -------------------------------------------------
if ($Clean) {
    Write-Info "Cleaning build directories..."
    Remove-Item -Recurse -Force "$RepoRoot\build" -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force "$RepoRoot\build-tsan" -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force "$ProviderSimDir\build" -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force "$ProviderSimDir\build-tsan" -ErrorAction SilentlyContinue
}

Write-Host ""

# -------------------------------------------------
# Delegate Build
# -------------------------------------------------
Write-Info "Building project (Release)..."
& "$ScriptDir\build.ps1"

if ($LASTEXITCODE -ne 0) {
    Write-Fail "Build failed"
}

Write-Host ""
Write-Host "=============================================="
Write-Host "Setup Complete"
Write-Host "=============================================="
Write-Host ""
Write-Host "Next steps:"
Write-Host "  Run tests:     .\scripts\test.ps1"
Write-Host "  TSAN build:    .\scripts\build.ps1 -TSan -Debug"
Write-Host "  Run runtime:   .\scripts\run.ps1"
Write-Host ""
