@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo Building QuickBTTray Native C Port
echo ===================================================

:: Check if cl is already in PATH
where cl.exe >nul 2>&1
if %ERRORLEVEL% equ 0 goto COMPILE

:: Try locating vcvars64.bat via vswhere
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VS_PATH=%%i
    )
)

if defined VS_PATH (
    if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
        echo Initializing MSVC x64 environment...
        call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
        goto COMPILE
    )
)

echo ERROR: Could not locate MSVC x64 toolchain.
echo Please run this script from a Visual Studio Developer Command Prompt.
exit /b 1

:COMPILE
set SCRIPT_DIR=%~dp0
set SRC_DIR=%SCRIPT_DIR%src
set RES_DIR=%SCRIPT_DIR%res
set BIN_DIR=%SCRIPT_DIR%bin

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

echo Compiling resources...
rc.exe /nologo /fo "%BIN_DIR%\resource.res" "%RES_DIR%\resource.rc"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Resource compilation failed.
    exit /b %ERRORLEVEL%
)

echo Compiling and linking QuickBTTray.exe...
cl.exe /nologo /O2 /W4 /wd4201 /GL /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
    /Fe"%BIN_DIR%\QuickBTTray.exe" /Fo"%BIN_DIR%\\" ^
    "%SRC_DIR%\app_state.c" ^
    "%SRC_DIR%\startup.c" ^
    "%SRC_DIR%\theme.c" ^
    "%SRC_DIR%\media.c" ^
    "%SRC_DIR%\bluetooth.c" ^
    "%SRC_DIR%\ui_common.c" ^
    "%SRC_DIR%\ui_menu.c" ^
    "%SRC_DIR%\ui_settings.c" ^
    "%SRC_DIR%\main.c" ^
    "%BIN_DIR%\resource.res" ^
    /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF ^
    user32.lib gdi32.lib shell32.lib advapi32.lib bthprops.lib dwmapi.lib comctl32.lib ole32.lib

if %ERRORLEVEL% equ 0 (
    echo.
    echo ===================================================
    echo SUCCESS: %BIN_DIR%\QuickBTTray.exe built successfully!
    echo ===================================================
    del "%BIN_DIR%\*.obj" "%BIN_DIR%\*.res" >nul 2>&1
    dir "%BIN_DIR%\QuickBTTray.exe" | findstr /i "QuickBTTray.exe"
) else (
    echo.
    echo ERROR: Build failed.
    exit /b %ERRORLEVEL%
)
