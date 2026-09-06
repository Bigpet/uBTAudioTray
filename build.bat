@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo Building uBTAudioTray Native C Port
echo ===================================================

set "SCRIPT_DIR=%~dp0"
set "SRC_DIR=%SCRIPT_DIR%src"
set "RES_DIR=%SCRIPT_DIR%res"
set "BIN_DIR=%SCRIPT_DIR%bin"

:: Defaults
set "TARGET_ARCH=x64"
set "ENABLE_KS=1"
set "ENABLE_API=1"
set "ENABLE_UI=1"
set "OUT_FILE="
set "OUT_DIR="

:: Parse command line flags
:PARSE_ARGS
if "%~1"=="" goto ARGS_DONE
set "ARG=%~1"
if "!ARG:~0,2!"=="--" set "ARG=-!ARG:~2!"

:: Architecture flags
if /i "!ARG!"=="-arm64"   ( set "TARGET_ARCH=arm64" & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-arm"     ( set "TARGET_ARCH=arm64" & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-x64"     ( set "TARGET_ARCH=x64" & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-arch"    ( set "TARGET_ARCH=%~2" & shift & shift & goto PARSE_ARGS )

:: Output flags
if /i "!ARG!"=="-out"     ( set "OUT_FILE=%~2" & shift & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-outdir"  ( set "OUT_DIR=%~2" & shift & shift & goto PARSE_ARGS )

:: Feature flags
if /i "!ARG!"=="-no-ks"    ( set "ENABLE_KS=0" & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-no-api"   ( set "ENABLE_API=0" & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-no-ui"    ( set "ENABLE_UI=0" & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-only-ks"  ( set "ENABLE_KS=1" & set "ENABLE_API=0" & set "ENABLE_UI=0" & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-only-api" ( set "ENABLE_KS=0" & set "ENABLE_API=1" & set "ENABLE_UI=0" & shift & goto PARSE_ARGS )
if /i "!ARG!"=="-only-ui"  ( set "ENABLE_KS=0" & set "ENABLE_API=0" & set "ENABLE_UI=1" & shift & goto PARSE_ARGS )

shift
goto PARSE_ARGS

:ARGS_DONE
if %ENABLE_KS% equ 0 if %ENABLE_API% equ 0 if %ENABLE_UI% equ 0 (
    echo ERROR: At least one method must be enabled.
    exit /b 1
)

echo Target Arch:   !TARGET_ARCH!
echo Configuration: KS=!ENABLE_KS!, API/HCI=!ENABLE_API!, UI=!ENABLE_UI!

:: Check if cl is already in PATH with matching target architecture
set COMPILER_READY=0
where cl.exe >nul 2>&1
if %ERRORLEVEL% equ 0 (
    if /i "!VSCMD_ARG_TGT_ARCH!"=="!TARGET_ARCH!" (
        set COMPILER_READY=1
    )
)
if !COMPILER_READY! equ 1 goto COMPILE

:: Try locating VS via vswhere
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -property installationPath`) do (
        set VS_PATH=%%i
    )
)

if defined VS_PATH (
    if /i "!TARGET_ARCH!"=="arm64" (
        if exist "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" (
            echo Initializing MSVC ARM64 environment...
            call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64_arm64 >nul
            goto COMPILE
        )
    ) else (
        if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
            echo Initializing MSVC x64 environment...
            call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
            goto COMPILE
        ) else if exist "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" (
            echo Initializing MSVC x64 environment...
            call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
            goto COMPILE
        )
    )
)

echo ERROR: Could not locate MSVC !TARGET_ARCH! toolchain.
echo Please run this script from a Visual Studio Developer Command Prompt.
exit /b 1

:COMPILE
if not defined OUT_FILE (
    if defined OUT_DIR (
        set "OUT_FILE=!OUT_DIR!\uBTAudioTray.exe"
    ) else (
        set "OUT_FILE=%BIN_DIR%\uBTAudioTray.exe"
    )
)

:: Extract directory and filename for target executable
for %%F in ("!OUT_FILE!") do (
    set "OUT_DIR_ACTUAL=%%~dpF"
    set "OUT_FILENAME=%%~nxF"
)

if not exist "!OUT_DIR_ACTUAL!" mkdir "!OUT_DIR_ACTUAL!"

:: Dedicated object directory to avoid collisions
set "OBJ_DIR=%BIN_DIR%\obj_!TARGET_ARCH!_!ENABLE_UI!"
if not exist "!OBJ_DIR!" mkdir "!OBJ_DIR!"

:: Close running instance if any to release file lock
taskkill /IM "!OUT_FILENAME!" /F >nul 2>&1

echo Compiling resources...
rc.exe /nologo /fo "!OBJ_DIR!\resource.res" "%RES_DIR%\resource.rc"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Resource compilation failed.
    exit /b %ERRORLEVEL%
)

set METHOD_DEFINES=/DENABLE_KS=!ENABLE_KS! /DENABLE_API_HCI=!ENABLE_API! /DENABLE_UI=!ENABLE_UI!
set EXTRA_SOURCES=
set EXTRA_LIBS=
if !ENABLE_UI! equ 1 (
    set EXTRA_SOURCES="%SRC_DIR%\uia_connect.c"
    set EXTRA_LIBS=oleaut32.lib
)

echo Compiling and linking !OUT_FILENAME! ^(!TARGET_ARCH!^)...
cl.exe /nologo /O2 /W4 /wd4201 /GL /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS !METHOD_DEFINES! ^
    /Fe"!OUT_FILE!" /Fo"!OBJ_DIR!\\" ^
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
    "!OBJ_DIR!\resource.res" ^
    /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF ^
    user32.lib gdi32.lib shell32.lib advapi32.lib bthprops.lib dwmapi.lib comctl32.lib ole32.lib !EXTRA_LIBS!

if %ERRORLEVEL% equ 0 (
    echo.
    echo ===================================================
    echo SUCCESS: !OUT_FILE! built successfully!
    echo ===================================================
    rmdir /s /q "!OBJ_DIR!" >nul 2>&1
    dir "!OUT_FILE!" | findstr /i "!OUT_FILENAME!"
) else (
    echo.
    echo ERROR: Build failed.
    rmdir /s /q "!OBJ_DIR!" >nul 2>&1
    exit /b %ERRORLEVEL%
)
