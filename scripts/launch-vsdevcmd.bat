@echo off
REM Batch script to start Visual Studio Developer Command Prompt
REM Supports VS 2017-2026, all editions (Community/Professional/Enterprise/BuildTools)
REM Uses vswhere.exe first, then falls back to manual search

REM ============================================================================
REM Method 1: Try vswhere.exe (recommended - finds all editions automatically)
REM ============================================================================
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    REM Find latest Visual Studio with C++ tools (any edition, any version)
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_PATH=%%i"
    )
    
    if defined VS_INSTALL_PATH (
        set "DEV_CMD_PATH=%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvarsall.bat"
        
        if exist "%DEV_CMD_PATH%" (
            echo Launching Visual Studio Developer Command Prompt...
            call "%DEV_CMD_PATH%" x64
            exit /b 0
        )
    )
)

REM ============================================================================
REM Method 2: Manual search for VS 2017-2026 (all editions + BuildTools)
REM ============================================================================
set "VS_VERSIONS=18 2026 2025 2024 2023 2022 2021 2020 2019 2018 2017"
set "VS_EDITIONS=BuildTools Community Professional Enterprise"

for %%V in (%VS_VERSIONS%) do (
    for %%E in (%VS_EDITIONS%) do (
        REM Check Program Files (x86) first
        if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
            echo Launching Visual Studio Developer Command Prompt...
            call "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" x64
            exit /b 0
        )
        
        REM Check Program Files
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
            echo Launching Visual Studio Developer Command Prompt...
            call "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" x64
            exit /b 0
        )
    )
)

REM ============================================================================
REM Method 3: Fallback for legacy VS 2015 and earlier
REM ============================================================================
set "LEGACY_VERSIONS=14.0 12.0 11.0"

for %%L in (%LEGACY_VERSIONS%) do (
    REM Check Program Files (x86)
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio %%L\VC\vcvarsall.bat" (
        echo Launching Visual Studio Developer Command Prompt...
        call "%ProgramFiles(x86)%\Microsoft Visual Studio %%L\VC\vcvarsall.bat" x64
        exit /b 0
    )
    
    REM Check Program Files
    if exist "%ProgramFiles%\Microsoft Visual Studio %%L\VC\vcvarsall.bat" (
        echo Launching Visual Studio Developer Command Prompt...
        call "%ProgramFiles%\Microsoft Visual Studio %%L\VC\vcvarsall.bat" x64
        exit /b 0
    )
)

REM ============================================================================
REM Not found - show error
REM ============================================================================
echo Visual Studio Developer Command Prompt not found.
echo.
echo Please install one of the following:
echo   - Visual Studio 2017 or newer (Community/Professional/Enterprise)
echo   - Visual Studio Build Tools 2017 or newer
echo   - Ensure "Desktop development with C++" workload is installed
echo.
exit /b 1