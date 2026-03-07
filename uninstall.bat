@echo off
setlocal

set TASK_NAME=ParentClient
set EXE_NAME=ParentClient.exe
set INSTALL_DIR=%ProgramFiles%\ParentClient

:: Require elevation
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo This script requires administrator privileges.
    echo Please right-click and select "Run as administrator".
    pause
    exit /b 1
)

:: Stop any running instance
echo Stopping running instance (if any)...
taskkill /im "%EXE_NAME%" /f >nul 2>&1

:: Remove autostart registry entry
echo Removing startup entry...
reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "%TASK_NAME%" /f >nul 2>&1
echo Startup entry removed (or was not present).

:: Remove legacy scheduled task if present from an older install
schtasks /query /tn "%TASK_NAME%" >nul 2>&1
if %errorlevel% equ 0 (
    echo Removing legacy scheduled task "%TASK_NAME%"...
    schtasks /delete /tn "%TASK_NAME%" /f >nul 2>&1
)

:: Remove install directory
if exist "%INSTALL_DIR%" (
    echo Removing %INSTALL_DIR%...
    rmdir /s /q "%INSTALL_DIR%"
    if %errorlevel% neq 0 (
        echo ERROR: Failed to remove %INSTALL_DIR%.
        echo        Please close any open files in that folder and try again.
        pause
        exit /b 1
    )
    echo Installation directory removed.
) else (
    echo Install directory not found, skipping.
)

echo.
echo ============================================
echo  Uninstallation complete.
echo  ParentClient has been removed.
echo ============================================
echo.
pause
endlocal
