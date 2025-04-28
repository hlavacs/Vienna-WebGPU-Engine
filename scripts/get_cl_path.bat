@ECHO off
setlocal EnableDelayedExpansion

REM Path to vswhere
set VSWHERE_PATH="C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

REM Get VS installation path
for /f "usebackq tokens=*" %%i in (`%VSWHERE_PATH% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set VSINSTALL_PATH=%%i
)

if not defined VSINSTALL_PATH (
    echo [ERROR] Visual Studio with C++ tools not found.
    exit /b 1
)

REM Set up environment
call "%VSINSTALL_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

REM Find cl.exe
for /f "tokens=*" %%i in ('where cl.exe') do (
    set CLPATH=%%i
    goto :found
)

:found
if defined CLPATH (
    echo !CLPATH!
) else (
    echo [ERROR] cl.exe not found in PATH.
    exit /b 1
)

endlocal & set "%~1=%CLPATH%"