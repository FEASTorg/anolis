<#
.SYNOPSIS
    Run clang-format over C++ sources.

.DESCRIPTION
    Formats selected directories relative to repo root.
    Defaults to all if no switches are provided.
#>

param(
    [switch]$Core,
    [switch]$Tests
)

$ErrorActionPreference = "Stop"

# Resolve repo root (script located in scripts/)
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

$Dirs = @{
    Core  = Join-Path $RepoRoot "core"
    Tests = Join-Path $RepoRoot "tests"
}

# Determine targets (default: all)
$Targets = if (-not ($Core -or $Tests)) {
    $Dirs.Values
} else {
    @(
        if ($Core)  { $Dirs.Core }
        if ($Tests) { $Dirs.Tests }
    )
}

$Total = 0

foreach ($dir in $Targets) {
    if (Test-Path $dir) {
        $Files = Get-ChildItem $dir -Recurse -Include *.cpp,*.hpp -File
        if ($Files) {
            Write-Host "Formatting $($Files.Count) file(s) in $dir"
            $Files | ForEach-Object { clang-format -i $_.FullName }
            $Total += $Files.Count
        }
    }
}

Write-Host ""
Write-Host "Formatted $Total file(s)."
