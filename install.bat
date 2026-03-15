@echo off
setlocal enabledelayedexpansion

:: ---------------------------------------------------------------
::  Edit DOWNLOAD_URL to point to your hosted ParentClient.exe
:: ---------------------------------------------------------------
set DOWNLOAD_URL=https://github.com/Bravos-World/parental-v2-client/releases/download/1.0.0/ParentClient.exe
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
::  Create install directory (remove old one first)
:: ---------------------------------------------------------------
if exist "%INSTALL_DIR%" (
    echo Stopping running instance (if any)...
    taskkill /im "%EXE_NAME%" /f >nul 2>&1
    echo Removing existing installation directory...
    rmdir /s /q "%INSTALL_DIR%"
    if exist "%INSTALL_DIR%" (
        echo ERROR: Failed to remove %INSTALL_DIR%.
        echo        Close any programs using files in that folder and try again.
        pause
        exit /b 1
    )
)
mkdir "%INSTALL_DIR%"
if %errorlevel% neq 0 (
    echo ERROR: Failed to create directory %INSTALL_DIR%
    pause
    exit /b 1
)

:: Copy executable
echo Copying %EXE_NAME% to %INSTALL_DIR%...
copy /y "%~dp0%EXE_NAME%" "%INSTALL_DIR%\%EXE_NAME%" >nul
if %errorlevel% neq 0 (
    echo ERROR: Failed to copy %EXE_NAME%.
    pause
    exit /b 1
)

:: Write launcher.vbs - waits for internet connectivity then launches app silently
echo Writing launcher.vbs...
> "%INSTALL_DIR%\launcher.vbs" (
    echo Dim shell, fso, exePath, http, i
    echo Set shell = CreateObject^("WScript.Shell"^)
    echo Set fso = CreateObject^("Scripting.FileSystemObject"^)
    echo exePath = fso.GetParentFolderName^(WScript.ScriptFullName^) ^& "\ParentClient.exe"
    echo For i = 1 To 60
    echo     On Error Resume Next
    echo     Set http = CreateObject^("MSXML2.ServerXMLHTTP.6.0"^)
    echo     http.Open "HEAD", "http://clients3.google.com/generate_204", False
    echo     http.setTimeouts 0, 3000, 3000, 3000
    echo     http.Send
    echo     If Err.Number = 0 And http.Status ^> 0 Then
    echo         Exit For
    echo     End If
    echo     On Error GoTo 0
    echo     WScript.Sleep 2000
    echo Next
    echo shell.Run Chr^(34^) ^& exePath ^& Chr^(34^), 0, False
)
if %errorlevel% neq 0 (
    echo ERROR: Failed to write launcher.vbs.
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
::  Register autostart via registry (runs for all users at logon,
::  in each user's own desktop session — required for GUI apps)
:: ---------------------------------------------------------------
set "TASK_EXE=%INSTALL_DIR%\%EXE_NAME%"

:: Remove legacy scheduled task from a previous install if present
schtasks /query /tn "%TASK_NAME%" >nul 2>&1
if %errorlevel% equ 0 (
    echo Removing legacy scheduled task...
    schtasks /delete /tn "%TASK_NAME%" /f >nul 2>&1
)

echo Registering startup entry...
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "%TASK_NAME%" /t REG_SZ /d "wscript.exe /nologo \"%INSTALL_DIR%\launcher.vbs\"" /f >nul 2>&1

if %errorlevel% neq 0 (
    echo ERROR: Failed to register startup entry.
    pause
    exit /b 1
)

echo Launching application...
start "" "%TASK_EXE%"

echo.
echo ============================================
echo  Installation complete.
echo  Installed to : %INSTALL_DIR%
echo  %EXE_NAME% is now running and will auto-start
echo  at logon once internet connectivity is available.
echo ============================================
echo.
pause
endlocal
