#!/usr/bin/env pwsh
# Anolis developer convenience wrapper (non-authoritative).
# Delegates to build + test preset wrappers.

[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Preset = "",
    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"

if ($Help) {
    Get-Content $MyInvocation.MyCommand.Path | Select-Object -First 16
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

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildArgs = @("-Preset", $Preset)
if ($Clean) {
    $buildArgs += "-Clean"
}
if ($ExtraArgs) {
    $buildArgs += $ExtraArgs
}

& (Join-Path $scriptDir "build.ps1") @buildArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $SkipTests) {
    & (Join-Path $scriptDir "test.ps1") -Preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
