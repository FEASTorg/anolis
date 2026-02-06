# Anolis HTTP API - PowerShell Examples
# Usage: .\examples_http.ps1 [-BaseUrl http://127.0.0.1:8080]
#
# Demonstrates all HTTP API endpoints
# Ensure the Anolis runtime is running via:
# .\scripts\run.ps1
# before executing this script

param(
    [string]$BaseUrl = "http://127.0.0.1:8080"
)

function Invoke-Api {
    param(
        [string]$Method,
        [string]$Path,
        [object]$Body = $null
    )
    
    Write-Host ""
    Write-Host "----------------------------------------" -ForegroundColor Cyan
    Write-Host "$Method $Path" -ForegroundColor Yellow
    Write-Host "----------------------------------------" -ForegroundColor Cyan
    
    try {
        if ($Method -eq "GET") {
            $response = Invoke-RestMethod -Uri "$BaseUrl$Path" -Method Get
        }
        else {
            $response = Invoke-RestMethod -Uri "$BaseUrl$Path" -Method $Method -Body ($Body | ConvertTo-Json -Depth 10) -ContentType "application/json"
        }
        $response | ConvertTo-Json -Depth 10
    }
    catch {
        Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
        if ($_.ErrorDetails.Message) {
            $_.ErrorDetails.Message | ConvertFrom-Json | ConvertTo-Json -Depth 5
        }
    }
}

Write-Host "========================================"
Write-Host "Anolis HTTP API Examples"
Write-Host "Base URL: $BaseUrl"
Write-Host "========================================"

# 1. Runtime Status
Write-Host "`n=== 1. Runtime Status ===" -ForegroundColor Green
Invoke-Api -Method GET -Path "/v0/runtime/status"

# 2. List Devices
Write-Host "`n=== 2. List Devices ===" -ForegroundColor Green
Invoke-Api -Method GET -Path "/v0/devices"

# 3. Device Capabilities
Write-Host "`n=== 3. Temperature Controller Capabilities ===" -ForegroundColor Green
Invoke-Api -Method GET -Path "/v0/devices/sim0/tempctl0/capabilities"

Write-Host "`n=== 4. Motor Controller Capabilities ===" -ForegroundColor Green
Invoke-Api -Method GET -Path "/v0/devices/sim0/motorctl0/capabilities"

# 4. Get All State
Write-Host "`n=== 5. Get All State ===" -ForegroundColor Green
Invoke-Api -Method GET -Path "/v0/state"

# 5. Get Single Device State
Write-Host "`n=== 6. Get Motor Controller State ===" -ForegroundColor Green
Invoke-Api -Method GET -Path "/v0/state/sim0/motorctl0"

# 6. Control Examples
Write-Host "`n=== 7. Set Motor 1 Duty to 50% ===" -ForegroundColor Green
Invoke-Api -Method POST -Path "/v0/call" -Body @{
    provider_id = "sim0"
    device_id   = "motorctl0"
    function_id = 10
    args        = @{
        motor_index = @{ type = "int64"; int64 = 1 }
        duty        = @{ type = "double"; double = 0.5 }
    }
}

Write-Host "`n=== 8. Set Motor 2 Duty to 75% ===" -ForegroundColor Green
Invoke-Api -Method POST -Path "/v0/call" -Body @{
    provider_id = "sim0"
    device_id   = "motorctl0"
    function_id = 10
    args        = @{
        motor_index = @{ type = "int64"; int64 = 2 }
        duty        = @{ type = "double"; double = 0.75 }
    }
}

Write-Host "`n=== 9. Set Temperature Setpoint to 45°C ===" -ForegroundColor Green
Invoke-Api -Method POST -Path "/v0/call" -Body @{
    provider_id = "sim0"
    device_id   = "tempctl0"
    function_id = 2
    args        = @{
        value = @{ type = "double"; double = 45.0 }
    }
}

Write-Host "`n=== 10. Verify State Changed ===" -ForegroundColor Green
Start-Sleep -Milliseconds 500
Invoke-Api -Method GET -Path "/v0/state"

Write-Host ""
Write-Host "========================================"
Write-Host "Examples Complete"
Write-Host "========================================"
