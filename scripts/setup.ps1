#!/usr/bin/env pwsh
# Anolis setup wrapper.
# Installs prerequisites, validates local environment, then delegates to build wrapper.

[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$Clean,
    [string]$Preset = ""
)

$ErrorActionPreference = "Stop"

if (-not $Preset) {
    if ($env:OS -eq "Windows_NT") {
        $Preset = "dev-windows-release"
    }
    else {
        $Preset = "dev-release"
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw "cmake not found" }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "git not found" }
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { throw "python not found" }

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is not set. Run setup in an environment where vcpkg is installed."
}
if (-not (Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake")) {
    throw "vcpkg toolchain not found under VCPKG_ROOT: $env:VCPKG_ROOT"
}

$lockFile = Join-Path $repoRoot "requirements-lock.txt"
if (Test-Path $lockFile) {
    python -m pip install --upgrade pip
    python -m pip install -r $lockFile
}

Push-Location $repoRoot
git submodule update --init --recursive
Pop-Location

if ($Clean) {
    Remove-Item -Recurse -Force (Join-Path $repoRoot "build") -ErrorAction SilentlyContinue
}

& (Join-Path $scriptDir "build.ps1") -Preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
