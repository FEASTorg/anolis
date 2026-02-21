# Anolis Development Setup Script (Windows)
#
# Bootstraps local prerequisites and performs a preset-driven build.
#
# Usage:
#   .\scripts\setup.ps1 [-Clean] [-Preset <name>]

param(
    [switch]$Clean,
    [string]$Preset = ""
)

$ErrorActionPreference = "Stop"

function Get-DefaultPreset {
    if ($env:OS -eq "Windows_NT") {
        return "dev-windows-release"
    }
    return "dev-release"
}

if (-not $Preset) {
    $Preset = Get-DefaultPreset
}
if (($env:OS -eq "Windows_NT") -and $Preset -in @("dev-release", "dev-debug")) {
    throw "Preset '$Preset' uses Ninja and may select MinGW on Windows. Use 'dev-windows-release', 'dev-windows-debug', or 'ci-windows-release'."
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

function Write-Info { param($m) Write-Host "[INFO]  $m" -ForegroundColor Green }
function Write-Warn { param($m) Write-Host "[WARN]  $m" -ForegroundColor Yellow }
function Write-Fail { param($m) Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

function Get-VcpkgBaseline {
    $vcpkgConfig = Join-Path $RepoRoot "vcpkg-configuration.json"
    if (Test-Path $vcpkgConfig) {
        try {
            $json = Get-Content $vcpkgConfig | ConvertFrom-Json
            if ($json.'default-registry'.baseline) {
                return $json.'default-registry'.baseline
            }
        }
        catch {}
    }
    Write-Fail "Unable to determine vcpkg baseline from vcpkg-configuration.json"
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
# Clean
# -------------------------------------------------
if ($Clean) {
    Write-Info "Cleaning build directory root..."
    Remove-Item -Recurse -Force "$RepoRoot\build" -ErrorAction SilentlyContinue
}

Write-Host ""

# -------------------------------------------------
# Delegate Build
# -------------------------------------------------
Write-Info "Building project (preset: $Preset)..."
& "$ScriptDir\build.ps1" -Preset $Preset

if ($LASTEXITCODE -ne 0) {
    Write-Fail "Build failed"
}

Write-Host ""
Write-Host "=============================================="
Write-Host "Setup Complete"
Write-Host "=============================================="
Write-Host ""
Write-Host "Next steps:"
Write-Host "  Run tests:     .\scripts\test.ps1 -Preset $Preset"
Write-Host "  Run runtime:   .\scripts\run.ps1 -Preset $Preset"
Write-Host ""
