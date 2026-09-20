@echo off
setlocal

set "KRITA_DEV_ROOT=%~dp0..\krita-dev"
set "KRITA_ENV=%KRITA_DEV_ROOT%\env.bat"
set "PYTHON_ACTIVATE=%KRITA_DEV_ROOT%\PythonEnv\Scripts\activate.bat"
set "KRITA_EXE=%KRITA_DEV_ROOT%\_install\bin\krita.exe"

if not exist "%KRITA_ENV%" (
    echo Krita development environment script not found:
    echo   "%KRITA_ENV%"
    exit /b 1
)

if not exist "%PYTHON_ACTIVATE%" (
    echo Python environment activation script not found:
    echo   "%PYTHON_ACTIVATE%"
    exit /b 1
)

if not exist "%KRITA_EXE%" (
    echo Krita executable not found:
    echo   "%KRITA_EXE%"
    echo Build and install Krita before running this launcher.
    exit /b 1
)

call "%KRITA_ENV%"
if errorlevel 1 (
    echo Failed to activate the Krita development environment.
    exit /b 1
)

call "%PYTHON_ACTIVATE%"
if errorlevel 1 (
    echo Failed to activate the Krita Python environment.
    exit /b 1
)

start "" /D "%KRITA_DEV_ROOT%\_install\bin" "%KRITA_EXE%" %*
