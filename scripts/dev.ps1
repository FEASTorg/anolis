# Anolis Development Environment Launcher
#
# One-command development setup with full observability stack.
# Starts: Runtime + Operator UI + InfluxDB + Grafana
#
# Usage:
#   .\scripts\dev.ps1                 # Full stack
#   .\scripts\dev.ps1 -SkipInfra      # No Docker (runtime + UI only)
#   .\scripts\dev.ps1 -NoUI           # No operator UI server
#   .\scripts\dev.ps1 -SkipBuild      # Don't check/rebuild if needed
#   .\scripts\dev.ps1 -Config PATH    # Custom config file

param(
    [switch]$SkipBuild,
    [switch]$SkipInfra,
    [switch]$NoUI,
    [string]$Config,
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"

# ANSI colors
$Red = "`e[31m"
$Green = "`e[32m"
$Yellow = "`e[33m"
$Cyan = "`e[36m"
$Reset = "`e[0m"

function Write-Header { Write-Host $args -ForegroundColor Cyan }
function Write-Success { Write-Host "✓ $args" -ForegroundColor Green }
function Write-Warning { Write-Host "⚠ $args" -ForegroundColor Yellow }
function Write-Error { Write-Host "✗ $args" -ForegroundColor Red }
function Write-Step { Write-Host "►" -NoNewline -ForegroundColor Cyan; Write-Host " $args" }

# Paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}

if (-not $Config) {
    $Config = Join-Path $RepoRoot "anolis-runtime.yaml"
}

$DockerDir = Join-Path $RepoRoot "tools\docker"
$UIDir = Join-Path $RepoRoot "tools\operator-ui"

# Global cleanup tracking
$UIServerJob = $null
$RuntimeProcess = $null

# ============================================================================
# Cleanup Handler
# ============================================================================
function Cleanup {
    Write-Host ""
    Write-Step "Shutting down..."

    # Stop UI server
    if ($null -ne $UIServerJob) {
        Write-Step "Stopping operator UI server..."
        Stop-Job -Job $UIServerJob -ErrorAction SilentlyContinue
        Remove-Job -Job $UIServerJob -ErrorAction SilentlyContinue
        Write-Success "UI server stopped"
    }

    # Stop runtime (if we started it)
    if ($null -ne $RuntimeProcess -and -not $RuntimeProcess.HasExited) {
        Write-Step "Stopping runtime..."
        $RuntimeProcess.Kill()
        $RuntimeProcess.WaitForExit(5000)
        Write-Success "Runtime stopped"
    }

    # Auto-stop Docker containers
    if (-not $SkipInfra) {
        Write-Step "Stopping Docker stack..."
        Push-Location $DockerDir
        docker compose -f docker-compose.observability.yml down 2>&1 | Out-Null
        Pop-Location
        Write-Success "Docker stack stopped"
    }

    Write-Host ""
    Write-Success "Cleanup complete"
}

# Register cleanup on exit
$script:CleanupRegistered = $false

function Invoke-CleanupOnce {
    if (-not $script:CleanupRegistered) {
        $script:CleanupRegistered = $true
        Cleanup
    }
}

# Handle Ctrl+C properly
$null = Register-ObjectEvent `
    -InputObject ([Console]) `
    -EventName CancelKeyPress `
    -Action {
        Write-Host "`nCtrl+C detected"
        Invoke-CleanupOnce
        exit 0
    }

# Also handle normal PowerShell exit
Register-EngineEvent PowerShell.Exiting -Action { Invoke-CleanupOnce } | Out-Null


# ============================================================================
# Header
# ============================================================================
Write-Host ""
Write-Host "╔════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  Anolis Development Environment                ║" -ForegroundColor Cyan
Write-Host "╚════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ============================================================================
# Step 1: Validate Build
# ============================================================================
Write-Step "Checking build..."

# Try Release, then Debug, then single-config
$Runtime = Join-Path $BuildDir "core\Release\anolis-runtime.exe"
if (-not (Test-Path $Runtime)) {
    $Runtime = Join-Path $BuildDir "core\Debug\anolis-runtime.exe"
}
if (-not (Test-Path $Runtime)) {
    $Runtime = Join-Path $BuildDir "core\anolis-runtime.exe"
}

if (-not (Test-Path $Runtime)) {
    Write-Warning "Runtime not built"
    
    if ($SkipBuild) {
        Write-Error "Runtime not found and -SkipBuild specified"
        exit 1
    }

    $response = Read-Host "Build now? (Y/n)"
    if ($response -eq "n" -or $response -eq "N") {
        Write-Error "Cannot continue without runtime"
        exit 1
    }

    Write-Step "Building..."
    & (Join-Path $ScriptDir "build.ps1")
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed"
        exit 1
    }

    # Re-check after build
    $Runtime = Join-Path $BuildDir "core\Release\anolis-runtime.exe"
    if (-not (Test-Path $Runtime)) {
        $Runtime = Join-Path $BuildDir "core\Debug\anolis-runtime.exe"
    }
    if (-not (Test-Path $Runtime)) {
        $Runtime = Join-Path $BuildDir "core\anolis-runtime.exe"
    }

    if (-not (Test-Path $Runtime)) {
        Write-Error "Build succeeded but runtime not found"
        exit 1
    }
}

Write-Success "Runtime found: $Runtime"

# Validate config
if (-not (Test-Path $Config)) {
    Write-Error "Config file not found: $Config"
    exit 1
}
Write-Success "Config found: $Config"

# ============================================================================
# Step 2: Start Infrastructure (Optional)
# ============================================================================
if (-not $SkipInfra) {
    Write-Host ""
    Write-Step "Starting observability stack (InfluxDB + Grafana)..."

    # Check Docker
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        Write-Warning "Docker not found - skipping infrastructure"
        Write-Warning "Install Docker Desktop to enable telemetry visualization"
        $SkipInfra = $true
    } else {
        Push-Location $DockerDir

        # Start containers
        docker compose -f docker-compose.observability.yml up -d 2>&1 | Out-Null
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Docker stack started"
            
            # Wait for health check
            Write-Step "Waiting for InfluxDB to be ready..."
            $waited = 0
            $healthy = $false
            while ($waited -lt 30 -and -not $healthy) {
                Start-Sleep -Seconds 2
                $waited += 2
                
                $status = docker compose -f docker-compose.observability.yml ps --format json 2>$null
                if ($status) {
                    $containers = $status | ConvertFrom-Json
                    $influx = $containers | Where-Object { $_.Service -eq "influxdb" }
                    if ($influx -and $influx.Health -eq "healthy") {
                        $healthy = $true
                    }
                }
            }

            if ($healthy) {
                Write-Success "InfluxDB ready"
            } else {
                Write-Warning "InfluxDB health check timeout (may still be starting)"
            }
        } else {
            Write-Warning "Failed to start Docker stack"
            $SkipInfra = $true
        }

        Pop-Location
    }
} else {
    Write-Warning "Skipping infrastructure (-SkipInfra)"
}

# ============================================================================
# Step 3: Start Operator UI Server (Optional)
# ============================================================================
if (-not $NoUI) {
    Write-Host ""
    Write-Step "Starting operator UI server (port 3000)..."

    # Check Python
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) {
        $python = Get-Command python3 -ErrorAction SilentlyContinue
    }

    if ($python) {
        # Start Python HTTP server in background
        $UIServerJob = Start-Job -ScriptBlock {
            param($UIDir)
            Set-Location $UIDir
            python -m http.server 3000 2>&1 | Out-Null
        } -ArgumentList $UIDir

        Start-Sleep -Seconds 1

        if ($UIServerJob.State -eq "Running") {
            Write-Success "Operator UI server started at http://localhost:3000"
        } else {
            Write-Warning "Failed to start UI server"
            $UIServerJob = $null
        }
    } else {
        Write-Warning "Python not found - skipping UI server"
        Write-Warning "Install Python 3 to enable operator UI"
    }
} else {
    Write-Warning "Skipping operator UI (-NoUI)"
}

# ============================================================================
# Step 4: Print Dashboard
# ============================================================================
Write-Host ""
Write-Host "╔════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║  Development Environment Ready                 ║" -ForegroundColor Green
Write-Host "╠════════════════════════════════════════════════╣" -ForegroundColor Green
Write-Host "║  Runtime:  http://127.0.0.1:8080              ║" -ForegroundColor White

if (-not $NoUI -and $null -ne $UIServerJob) {
    Write-Host "║  Operator: http://localhost:3000              ║" -ForegroundColor White
}

if (-not $SkipInfra) {
    Write-Host "║  Grafana:  http://localhost:3001 (admin/...)  ║" -ForegroundColor White
    Write-Host "║  InfluxDB: http://localhost:8086 (admin/...)  ║" -ForegroundColor White
}

Write-Host "╠════════════════════════════════════════════════╣" -ForegroundColor Green
Write-Host "║  Press Ctrl+C to stop                          ║" -ForegroundColor Green
Write-Host "╚════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""

# ============================================================================
# Step 5: Start Runtime (Foreground)
# ============================================================================
Write-Step "Starting Anolis runtime..."
Write-Host ""

try {
    $RuntimeProcess = Start-Process `
        -FilePath $Runtime `
        -ArgumentList "--config=$Config" `
        -NoNewWindow `
        -PassThru

    $RuntimeProcess.WaitForExit()
    $exitCode = $RuntimeProcess.ExitCode
}
catch {
    Write-Error "Runtime crashed: $_"
    $exitCode = 1
}
finally {
    Invoke-CleanupOnce
    exit $exitCode
}
