@echo off
setlocal
set SCRIPT_DIR=%~dp0

:: Check if py.exe exists
where py.exe >nul 2>&1
if %ERRORLEVEL% equ 0 (
    py -3 "%SCRIPT_DIR%generate_screenshot.py" %*
    goto END
)

:: Check if python.exe exists
where python.exe >nul 2>&1
if %ERRORLEVEL% equ 0 (
    python "%SCRIPT_DIR%generate_screenshot.py" %*
    goto END
)

echo ERROR: Neither py.exe nor python.exe was found in PATH.
echo Please ensure Python 3 with Pillow is installed.
exit /b 1

:END
