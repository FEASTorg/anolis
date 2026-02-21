#!/usr/bin/env pwsh
# Anolis build wrapper (preset-first)
#
# Usage:
#   .\scripts\build.ps1 [options] [-- <extra-cmake-configure-args>]
#
# Options:
#   -Preset <name>     Configure/build preset (default: dev-release on Linux/macOS, dev-windows-release on Windows)
#   -Clean             Remove preset build directory before configure
#   -Jobs <N>          Parallel build jobs
#   -Help              Show help

[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Preset = "",
    [switch]$Clean,
    [int]$Jobs,
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"

function Get-DefaultPreset {
    if ($env:OS -eq "Windows_NT") {
        return "dev-windows-release"
    }
    return "dev-release"
}

function Assert-PresetAllowed {
    param([string]$RequestedPreset)

    if (($env:OS -eq "Windows_NT") -and $RequestedPreset -in @("dev-release", "dev-debug")) {
        throw "Preset '$RequestedPreset' uses Ninja and may select MinGW on Windows. Use 'dev-windows-release', 'dev-windows-debug', or 'ci-windows-release'."
    }
}

for ($i = 0; $i -lt $ExtraArgs.Count; $i++) {
    $arg = $ExtraArgs[$i]
    switch -Regex ($arg) {
        '^--help$' { $Help = $true; continue }
        '^--clean$' { $Clean = $true; continue }
        '^--preset$' {
            if ($i + 1 -ge $ExtraArgs.Count) { throw "--preset requires a value" }
            $i++
            $Preset = $ExtraArgs[$i]
            continue
        }
        '^--preset=(.+)$' { $Preset = $Matches[1]; continue }
        '^(-j|--jobs)$' {
            if ($i + 1 -ge $ExtraArgs.Count) { throw "$arg requires a value" }
            $i++
            $Jobs = [int]$ExtraArgs[$i]
            continue
        }
        '^--jobs=(.+)$' { $Jobs = [int]$Matches[1]; continue }
        '^--$' {
            if ($i + 1 -lt $ExtraArgs.Count) {
                $ExtraArgs = $ExtraArgs[($i + 1) .. ($ExtraArgs.Count - 1)]
            }
            else {
                $ExtraArgs = @()
            }
            break
        }
        default {
            throw "Unknown argument: $arg"
        }
    }
}

if ($Help) {
    Get-Content $MyInvocation.MyCommand.Path | Select-Object -First 22
    exit 0
}

if (-not $Preset) {
    $Preset = Get-DefaultPreset
}
Assert-PresetAllowed -RequestedPreset $Preset

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$buildDir = Join-Path $repoRoot "build\$Preset"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[INFO] Cleaning build directory: $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

Write-Host "[INFO] Configure preset: $Preset"
& cmake --preset $Preset @ExtraArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[INFO] Build preset: $Preset"
if ($Jobs -gt 0) {
    & cmake --build --preset $Preset --parallel $Jobs
}
else {
    & cmake --build --preset $Preset --parallel
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[INFO] Build complete" -ForegroundColor Green
