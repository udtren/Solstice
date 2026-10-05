@echo off
setlocal
rem SPDX-FileCopyrightText: 2026 Krita contributors
rem SPDX-License-Identifier: GPL-3.0-or-later
rem User-invoked launcher only. Never stops an existing application.
if "%~1"=="" goto usage
set "solsticeTraceMode=%~2"
if "%solsticeTraceMode%"=="" set "solsticeTraceMode=brush"
if "%solsticeTraceMode%"=="cpu" goto configured
if "%solsticeTraceMode%"=="projection" goto configured
if "%solsticeTraceMode%"=="brush" goto configured
goto usage
:configured
if not exist "%~1\env.bat" goto failed
if not exist "%~1\_install\bin\krita.com" goto failed
call "%~1\env.bat" >nul 2>&1
if errorlevel 1 goto failed
call "%~1\PythonEnv\Scripts\activate.bat" >nul 2>&1
if errorlevel 1 goto failed
set "KRITA_GPU_PROJECTION=1"
set "KRITA_GPU_BRUSH=0"
if "%solsticeTraceMode%"=="cpu" set "KRITA_GPU_PROJECTION=0"
if "%solsticeTraceMode%"=="brush" set "KRITA_GPU_BRUSH=1"
set "KRITA_GPU_VALIDATION=0"
set "KRITA_GPU_BRUSH_DEBUG="
set "KRITA_GPU_CANVAS_DEBUG="
set "QT_FORCE_STDERR_LOGGING=1"
set "KRITA_PAINT_TRACE=%TEMP%\solstice-paint-trace-%solsticeTraceMode%"
if /i "%~3"=="--check" (
    echo Paint trace launcher environment OK. Application was not started.
    exit /b 0
)
powershell.exe -NoProfile -Command "if (Get-Process krita,krita.com -ErrorAction SilentlyContinue) { exit 1 }"
if errorlevel 1 (
    echo Close Solstice before starting a separate measurement process.
    pause
    exit /b 1
)
pushd "%~1\_install\bin"
if errorlevel 1 goto failed
krita.com > "%KRITA_PAINT_TRACE%.launch.log" 2>&1
set "solsticeTraceExit=%errorlevel%"
popd
echo Application exit code: %solsticeTraceExit%
echo Trace prefix: %KRITA_PAINT_TRACE%
echo A normal exit writes a JSON file with the process ID in its name.
pause
exit /b %solsticeTraceExit%
:usage
echo Usage: run.cmd ^<krita-dev-root^> [cpu^|projection^|brush] [--check]
exit /b 1
:failed
echo Failed to prepare the Solstice trace environment.
if /i not "%~3"=="--check" pause
exit /b 1
