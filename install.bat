@echo off
setlocal enabledelayedexpansion

:: ---------------------------------------------------------------
::  Edit DOWNLOAD_URL to point to your hosted ParentClient.exe
:: ---------------------------------------------------------------
set DOWNLOAD_URL=https://your-server.com/releases/ParentClient.exe
:: ---------------------------------------------------------------

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

:: Download exe if not found next to this script
if not exist "%~dp0%EXE_NAME%" (
    echo %EXE_NAME% not found locally.
    echo Downloading from: %DOWNLOAD_URL%
    echo.
    powershell -NoProfile -Command ^
        "Invoke-WebRequest -Uri '%DOWNLOAD_URL%' -OutFile '%~dp0%EXE_NAME%' -UseBasicParsing"
    if not exist "%~dp0%EXE_NAME%" (
        echo ERROR: Download failed.
        echo        Check DOWNLOAD_URL in this script and your internet connection.
        pause
        exit /b 1
    )
    echo Download complete.
    echo.
)

:: ---------------------------------------------------------------
::  Server configuration prompts
:: ---------------------------------------------------------------
echo ============================================
echo  Server Configuration
echo ============================================
echo.

set CFG_SCHEME=wss
set /p CFG_SCHEME=Server scheme (ws/wss) [default: wss]: 

set CFG_HOST=
set /p CFG_HOST=Server host (e.g. example.com): 
if "!CFG_HOST!"=="" (
    echo ERROR: Server host cannot be empty.
    pause
    exit /b 1
)

set CFG_PORT=443
set /p CFG_PORT=Server port [default: 443]: 

set CFG_SECRET=
set /p CFG_SECRET=Secret key: 

set CFG_PIN=1234
set /p CFG_PIN=Unlock PIN [default: 1234]: 

echo.

:: ---------------------------------------------------------------
::  Create install directory
:: ---------------------------------------------------------------
if not exist "%INSTALL_DIR%" (
    mkdir "%INSTALL_DIR%"
    if %errorlevel% neq 0 (
        echo ERROR: Failed to create directory %INSTALL_DIR%
        pause
        exit /b 1
    )
)

:: Copy executable
echo Copying %EXE_NAME% to %INSTALL_DIR%...
copy /y "%~dp0%EXE_NAME%" "%INSTALL_DIR%\%EXE_NAME%" >nul
if %errorlevel% neq 0 (
    echo ERROR: Failed to copy %EXE_NAME%.
    pause
    exit /b 1
)

:: Write config.ini from user input
echo Writing config.ini...
(
    echo SERVER_SCHEME=!CFG_SCHEME!
    echo SERVER_HOST=!CFG_HOST!
    echo SERVER_PORT=!CFG_PORT!
    echo SECRET_KEY=!CFG_SECRET!
    echo UNLOCK_PIN=!CFG_PIN!
) > "%INSTALL_DIR%\config.ini"
if %errorlevel% neq 0 (
    echo ERROR: Failed to write config.ini.
    pause
    exit /b 1
)

:: ---------------------------------------------------------------
::  Register scheduled task
:: ---------------------------------------------------------------
schtasks /query /tn "%TASK_NAME%" >nul 2>&1
if %errorlevel% equ 0 (
    echo Removing existing scheduled task...
    schtasks /delete /tn "%TASK_NAME%" /f >nul
)

echo Registering scheduled task "%TASK_NAME%"...
schtasks /create ^
    /tn "%TASK_NAME%" ^
    /tr "\"%INSTALL_DIR%\%EXE_NAME%\"" ^
    /sc ONLOGON ^
    /rl HIGHEST ^
    /f >nul

if %errorlevel% neq 0 (
    echo ERROR: Failed to register scheduled task.
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Installation complete.
echo  Installed to : %INSTALL_DIR%
echo  Task name    : %TASK_NAME%
echo  %EXE_NAME% will start automatically on next logon.
echo ============================================
echo.
pause
endlocal
