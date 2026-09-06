@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo Building All Release Targets for uBTAudioTray
echo ===================================================
echo 1. x64 normal
echo 2. ARM normal (ARM64)
echo 3. x64 with no uia
echo ===================================================

set "SCRIPT_DIR=%~dp0"
set "RELEASE_DIR=%SCRIPT_DIR%bin\release"
if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"

set ERRORS=0

:: ----------------------------------------------------
:: Target 1: x64 Normal
:: ----------------------------------------------------
echo.
echo [1/3] Building Target: x64 normal...
cmd.exe /c call "%SCRIPT_DIR%build.bat" -x64 -out "%RELEASE_DIR%\uBTAudioTray-x64.exe"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to build x64 normal target.
    set /a ERRORS+=1
)

:: ----------------------------------------------------
:: Target 2: ARM Normal (ARM64)
:: ----------------------------------------------------
echo.
echo [2/3] Building Target: ARM normal (ARM64)...
cmd.exe /c call "%SCRIPT_DIR%build.bat" -arm64 -out "%RELEASE_DIR%\uBTAudioTray-arm64.exe"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to build ARM normal target.
    set /a ERRORS+=1
)

:: ----------------------------------------------------
:: Target 3: x64 No UIA
:: ----------------------------------------------------
echo.
echo [3/3] Building Target: x64 with no uia...
cmd.exe /c call "%SCRIPT_DIR%build.bat" -x64 -no-ui -out "%RELEASE_DIR%\uBTAudioTray-x64-no-uia.exe"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to build x64 no-uia target.
    set /a ERRORS+=1
)

:: ----------------------------------------------------
:: Summary
:: ----------------------------------------------------
echo.
echo ===================================================
echo Release Build Summary
echo ===================================================
if %ERRORS% equ 0 (
    echo All targets built successfully!
    echo.
    echo Location: %RELEASE_DIR%
    echo -------------------------------------------------------------------------------
    for %%F in ("%RELEASE_DIR%\uBTAudioTray-x64.exe" "%RELEASE_DIR%\uBTAudioTray-arm64.exe" "%RELEASE_DIR%\uBTAudioTray-x64-no-uia.exe") do (
        if exist "%%F" (
            set "SIZE=%%~zF"
            echo   - %%~nxF  [!SIZE! bytes]
        )
    )
    echo -------------------------------------------------------------------------------
    exit /b 0
) else (
    echo ERROR: One or more targets failed to build.
    exit /b 1
)
