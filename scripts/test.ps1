#!/usr/bin/env pwsh
# Anolis test wrapper (preset-first)
#
# Usage:
#   .\scripts\test.ps1 [options] [-- <extra-ctest-args>]
#
# Options:
#   -Preset <name>    Test preset (default: dev-release on Linux/macOS, dev-windows-release on Windows)
#   -Label <expr>     Run only tests matching label expression (-L)
#   -VerboseOutput    Run ctest with -VV
#   -Help             Show help

[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Preset = "",
    [string]$Label,
    [switch]$VerboseOutput,
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"

for ($i = 0; $i -lt $ExtraArgs.Count; $i++) {
    $arg = $ExtraArgs[$i]
    switch -Regex ($arg) {
        '^--help$' { $Help = $true; continue }
        '^--preset$' {
            if ($i + 1 -ge $ExtraArgs.Count) { throw "--preset requires a value" }
            $i++
            $Preset = $ExtraArgs[$i]
            continue
        }
        '^--preset=(.+)$' { $Preset = $Matches[1]; continue }
        '^--label$' {
            if ($i + 1 -ge $ExtraArgs.Count) { throw "--label requires a value" }
            $i++
            $Label = $ExtraArgs[$i]
            continue
        }
        '^--label=(.+)$' { $Label = $Matches[1]; continue }
        '^(-v|--verbose)$' { $VerboseOutput = $true; continue }
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
    Get-Content $MyInvocation.MyCommand.Path | Select-Object -First 20
    exit 0
}

if (-not $Preset) {
    if ($env:OS -eq "Windows_NT") {
        $Preset = "dev-windows-release"
    }
    else {
        $Preset = "dev-release"
    }
}
if (($env:OS -eq "Windows_NT") -and $Preset -in @("dev-release", "dev-debug")) {
    throw "Preset '$Preset' uses Ninja and may select MinGW on Windows. Use 'dev-windows-release', 'dev-windows-debug', or 'ci-windows-release'."
}

$ctestArgs = @("--preset", $Preset)
if ($Label) {
    $ctestArgs += @("-L", $Label)
}
if ($VerboseOutput) {
    $ctestArgs += "-VV"
}
if ($ExtraArgs) {
    $ctestArgs += $ExtraArgs
}

Write-Host "[INFO] Test preset: $Preset"
& ctest @ctestArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
