@echo off
setlocal enabledelayedexpansion

:: ---------------------------------------------------------------
::  Edit DOWNLOAD_URL to point to your hosted ParentClient.exe
:: ---------------------------------------------------------------
set "DOWNLOAD_URL=https://github.com/Bravos-World/parental-v2-client/releases/download/1.0.0/ParentClient.exe"
:: ---------------------------------------------------------------

set "TASK_NAME=ParentClient"
set "EXE_NAME=ParentClient.exe"
set "INSTALL_DIR=%ProgramFiles%\ParentClient"

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
    powershell -NoProfile -Command "Invoke-WebRequest -Uri '%DOWNLOAD_URL%' -OutFile '%~dp0%EXE_NAME%' -UseBasicParsing"
    if not exist "%~dp0%EXE_NAME%" (
        echo ERROR: Download failed. Check your URL or connection.
        pause
        exit /b 1
    )
    echo Download complete.
)

:: ---------------------------------------------------------------
::  Server configuration prompts
:: ---------------------------------------------------------------
echo ============================================
echo  Server Configuration
echo ============================================
set "CFG_SCHEME=wss"
set /p "CFG_SCHEME=Server scheme (ws/wss) [default: wss]: "

set "CFG_HOST="
set /p "CFG_HOST=Server host (e.g. example.com): "
if "!CFG_HOST!"=="" (
    echo ERROR: Server host cannot be empty.
    pause
    exit /b 1
)

set "CFG_PORT=443"
set /p "CFG_PORT=Server port [default: 443]: "

set "CFG_SECRET="
set /p "CFG_SECRET=Secret key: "

set "CFG_PIN=1234"
set /p "CFG_PIN=Unlock PIN [default: 1234]: "

:: ---------------------------------------------------------------
::  Create install directory
:: ---------------------------------------------------------------
if exist "%INSTALL_DIR%" (
    echo Stopping running instance and cleaning old files...
    taskkill /im "%EXE_NAME%" /f >nul 2>&1
    timeout /t 2 >nul
    rmdir /s /q "%INSTALL_DIR%"
)
mkdir "%INSTALL_DIR%" 2>nul

:: Copy executable
copy /y "%~dp0%EXE_NAME%" "%INSTALL_DIR%\%EXE_NAME%" >nul

:: Write launcher.vbs (Fixed Escaping)
echo Writing launcher.vbs...
(
    echo Set shell = CreateObject^("WScript.Shell"^)
    echo Set fso = CreateObject^("Scripting.FileSystemObject"^)
    echo exePath = fso.GetParentFolderName^(WScript.ScriptFullName^) ^& "\%EXE_NAME%"
    echo For i = 1 To 60
    echo    On Error Resume Next
    echo    Set http = CreateObject^("MSXML2.ServerXMLHTTP.6.0"^)
    echo    http.Open "HEAD", "http://clients3.google.com/generate_204", False
    echo    http.Send
    echo    If Err.Number = 0 Then Exit For
    echo    On Error GoTo 0
    echo    WScript.Sleep 2000
    echo Next
    echo shell.Run Chr^(34^) ^& exePath ^& Chr^(34^), 0, False
) > "%INSTALL_DIR%\launcher.vbs"

:: Write config.ini
(
    echo SERVER_SCHEME=!CFG_SCHEME!
    echo SERVER_HOST=!CFG_HOST!
    echo SERVER_PORT=!CFG_PORT!
    echo SECRET_KEY=!CFG_SECRET!
    echo UNLOCK_PIN=!CFG_PIN!
) > "%INSTALL_DIR%\config.ini"

:: ---------------------------------------------------------------
::  Register autostart via registry
:: ---------------------------------------------------------------
echo Registering startup entry...
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" /v "%TASK_NAME%" /t REG_SZ /d "wscript.exe \"%INSTALL_DIR%\launcher.vbs\"" /f >nul

:: Start it now
start "" "%INSTALL_DIR%\%EXE_NAME%"

echo Done! Installed to %INSTALL_DIR%
pause