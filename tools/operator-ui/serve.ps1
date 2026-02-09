# Anolis Operator UI - Development Server
#
# Quick helper to serve the operator UI on http://localhost:3000
# Requires Python 3 installed
#
# Usage:
#   cd tools/operator-ui
#   .\serve.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Starting Operator UI server..." -ForegroundColor Cyan
Write-Host "URL: http://localhost:3000" -ForegroundColor Green
Write-Host ""
Write-Host "Press Ctrl+C to stop" -ForegroundColor Yellow
Write-Host ""

Set-Location $ScriptDir

# Try python or python3
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command python3 -ErrorAction SilentlyContinue
}

if (-not $python) {
    Write-Host "ERROR: Python not found" -ForegroundColor Red
    Write-Host "Install Python 3 to use this server" -ForegroundColor Yellow
    exit 1
}

python -m http.server 3000
