@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo Building uBTAudioTray Native C Port
echo ===================================================

:: Defaults
set ENABLE_KS=1
set ENABLE_API=1
set ENABLE_UI=1

:: Parse command line flags
:PARSE_ARGS
if "%~1"=="" goto ARGS_DONE
if /i "%~1"=="-no-ks" set ENABLE_KS=0
if /i "%~1"=="--no-ks" set ENABLE_KS=0
if /i "%~1"=="-no-api" set ENABLE_API=0
if /i "%~1"=="--no-api" set ENABLE_API=0
if /i "%~1"=="-no-ui" set ENABLE_UI=0
if /i "%~1"=="--no-ui" set ENABLE_UI=0

if /i "%~1"=="-only-ks" (
    set ENABLE_KS=1
    set ENABLE_API=0
    set ENABLE_UI=0
)
if /i "%~1"=="--only-ks" (
    set ENABLE_KS=1
    set ENABLE_API=0
    set ENABLE_UI=0
)
if /i "%~1"=="-only-api" (
    set ENABLE_KS=0
    set ENABLE_API=1
    set ENABLE_UI=0
)
if /i "%~1"=="--only-api" (
    set ENABLE_KS=0
    set ENABLE_API=1
    set ENABLE_UI=0
)
if /i "%~1"=="-only-ui" (
    set ENABLE_KS=0
    set ENABLE_API=0
    set ENABLE_UI=1
)
if /i "%~1"=="--only-ui" (
    set ENABLE_KS=0
    set ENABLE_API=0
    set ENABLE_UI=1
)
shift
goto PARSE_ARGS

:ARGS_DONE
if %ENABLE_KS% equ 0 if %ENABLE_API% equ 0 if %ENABLE_UI% equ 0 (
    echo ERROR: At least one method must be enabled.
    exit /b 1
)

echo Configuration: KS=%ENABLE_KS%, API/HCI=%ENABLE_API%, UI=%ENABLE_UI%

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

:: Close running instance if any to release file lock
taskkill /IM uBTAudioTray.exe /F >nul 2>&1

echo Compiling resources...
rc.exe /nologo /fo "%BIN_DIR%\resource.res" "%RES_DIR%\resource.rc"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Resource compilation failed.
    exit /b %ERRORLEVEL%
)

set METHOD_DEFINES=/DENABLE_KS=%ENABLE_KS% /DENABLE_API_HCI=%ENABLE_API% /DENABLE_UI=%ENABLE_UI%
set EXTRA_SOURCES=
set EXTRA_LIBS=
if %ENABLE_UI% equ 1 (
    set EXTRA_SOURCES="%SRC_DIR%\uia_connect.c"
    set EXTRA_LIBS=oleaut32.lib
)

echo Compiling and linking uBTAudioTray.exe...
cl.exe /nologo /O2 /W4 /wd4201 /GL /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS !METHOD_DEFINES! ^
    /Fe"%BIN_DIR%\uBTAudioTray.exe" /Fo"%BIN_DIR%\\" ^
    "%SRC_DIR%\app_state.c" ^
    "%SRC_DIR%\startup.c" ^
    "%SRC_DIR%\theme.c" ^
    "%SRC_DIR%\media.c" ^
    "%SRC_DIR%\bluetooth.c" ^
    "%SRC_DIR%\audio_state_notification.c" ^
    "%SRC_DIR%\ui_common.c" ^
    "%SRC_DIR%\ui_menu.c" ^
    "%SRC_DIR%\ui_settings.c" ^
    !EXTRA_SOURCES! ^
    "%SRC_DIR%\main.c" ^
    "%BIN_DIR%\resource.res" ^
    /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF ^
    user32.lib gdi32.lib shell32.lib advapi32.lib bthprops.lib dwmapi.lib comctl32.lib ole32.lib !EXTRA_LIBS!

if %ERRORLEVEL% equ 0 (
    echo.
    echo ===================================================
    echo SUCCESS: %BIN_DIR%\uBTAudioTray.exe built successfully!
    echo ===================================================
    del "%BIN_DIR%\*.obj" "%BIN_DIR%\*.res" >nul 2>&1
    dir "%BIN_DIR%\uBTAudioTray.exe" | findstr /i "uBTAudioTray.exe"
) else (
    echo.
    echo ERROR: Build failed.
    exit /b %ERRORLEVEL%
)
