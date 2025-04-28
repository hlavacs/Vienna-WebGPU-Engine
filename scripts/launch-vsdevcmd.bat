@echo off
REM Batch script to start Visual Studio Developer Command Prompt

REM Set the Visual Studio version (change this to your version)
SET VS_VERSION=2022

REM Define possible installation paths for different editions of Visual Studio
SET VS_PATHS="C:\Program Files (x86)\Microsoft Visual Studio\%VS_VERSION%\Community\\VC\Auxiliary\Build"
SET VS_PATHS=%VS_PATHS%;"C:\Program Files (x86)\Microsoft Visual Studio\%VS_VERSION%\Professional\VC\Auxiliary\Build"
SET VS_PATHS=%VS_PATHS%;"C:\Program Files (x86)\Microsoft Visual Studio\%VS_VERSION%\Enterprise\VC\Auxiliary\Build"
SET VS_PATHS=%VS_PATHS%;"C:\Program Files\Microsoft Visual Studio\%VS_VERSION%\Community\VC\Auxiliary\Build"
SET VS_PATHS=%VS_PATHS%;"C:\Program Files\Microsoft Visual Studio\%VS_VERSION%\Professional\VC\Auxiliary\Build"
SET VS_PATHS=%VS_PATHS%;"C:\Program Files\Microsoft Visual Studio\%VS_VERSION%\Enterprise\VC\Auxiliary\Build"

REM Loop through the paths and search for VsDevCmd.bat
FOR %%A IN (%VS_PATHS%) DO (
    IF EXIST "%%A\vcvarsall.bat" (
        SET DEV_CMD_PATH=%%A\vcvarsall.bat
        GOTO FOUND
    )
)

echo Visual Studio Developer Command Prompt not found.
exit /b 1

:FOUND
echo Launching Visual Studio Developer Command Prompt...
CALL %DEV_CMD_PATH% x64
