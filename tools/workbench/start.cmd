@echo off
cd /d "%~dp0\..\.."
echo Starting Anolis Workbench...
where python >nul 2>&1
if %ERRORLEVEL% equ 0 (
    python -m anolis_workbench_backend.server
) else (
    python3 -m anolis_workbench_backend.server
)
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Failed to start. Check output above.
    pause
)
