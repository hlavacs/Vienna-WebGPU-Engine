@echo off
setlocal

:: Args: example name (default main_demo), build type (default Debug),
::       backend: WGPU | DAWN | EMDAWN (default WGPU; EMSCRIPTEN = alias for EMDAWN),
::       dawn source mode: SOURCE builds Dawn from source instead of the vendored prebuilt.
set EXAMPLE_NAME=%1
set BUILDTYPE=%2
set WEBGPU_BACKEND=%3
set DAWN_MODE=%4
set PROFILE_HOST=Windows

if "%EXAMPLE_NAME%"=="" set EXAMPLE_NAME=main_demo
if "%BUILDTYPE%"=="" set BUILDTYPE=Debug
if "%WEBGPU_BACKEND%"=="" set WEBGPU_BACKEND=WGPU
if /I "%WEBGPU_BACKEND%"=="Emscripten" set WEBGPU_BACKEND=EMDAWN

if /I "%WEBGPU_BACKEND%"=="WGPU" goto :backend_ok
if /I "%WEBGPU_BACKEND%"=="DAWN" goto :backend_ok
if /I "%WEBGPU_BACKEND%"=="EMDAWN" goto :backend_ok
echo Invalid WEBGPU_BACKEND: %WEBGPU_BACKEND%
echo Must be WGPU, DAWN or EMDAWN
exit /b 1
:backend_ok

if /I "%WEBGPU_BACKEND%"=="EMDAWN" set PROFILE_HOST=Emscripten

:: Normalize paths - convert to absolute paths
set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%.."
set PROJECT_ROOT=%CD%
popd

set EXAMPLES_DIR=%PROJECT_ROOT%\examples
set EXAMPLE_SOURCE_DIR=%EXAMPLES_DIR%\%EXAMPLE_NAME%

:: Per-backend build dirs so switching backends never clobbers another tree.
:: DAWN+SOURCE keeps the legacy plain dir (preserves the expensive Dawn source builds).
set BUILD_DIR=%EXAMPLES_DIR%\build\%EXAMPLE_NAME%\%PROFILE_HOST%\%BUILDTYPE%-%WEBGPU_BACKEND%
if /I "%WEBGPU_BACKEND%"=="EMDAWN" set BUILD_DIR=%EXAMPLES_DIR%\build\%EXAMPLE_NAME%\%PROFILE_HOST%\%BUILDTYPE%
if /I not "%WEBGPU_BACKEND%"=="DAWN" goto :dir_done
if /I "%DAWN_MODE%"=="SOURCE" set BUILD_DIR=%EXAMPLES_DIR%\build\%EXAMPLE_NAME%\%PROFILE_HOST%\%BUILDTYPE%
:dir_done

if not exist "%EXAMPLE_SOURCE_DIR%\CMakeLists.txt" (
    echo [ERROR] Example '%EXAMPLE_NAME%' not found or has no CMakeLists.txt
    echo.
    echo Available examples:
    for /d %%d in ("%EXAMPLES_DIR%\*") do (
        if exist "%%d\CMakeLists.txt" echo   - %%~nxd
    )
    exit /b 1
)

echo ============================================
echo Building Example: %EXAMPLE_NAME%
echo Build Type: %BUILDTYPE%
echo Backend: %WEBGPU_BACKEND%
if /I "%WEBGPU_BACKEND%"=="DAWN" if /I "%DAWN_MODE%"=="SOURCE" echo Dawn mode: from source
if /I "%WEBGPU_BACKEND%"=="DAWN" if /I not "%DAWN_MODE%"=="SOURCE" echo Dawn mode: vendored prebuilt
echo Source Directory: %EXAMPLE_SOURCE_DIR%
echo Build Directory: %BUILD_DIR%
echo ============================================
echo.

if /I "%WEBGPU_BACKEND%"=="EMDAWN" goto :build_emdawn

call "%SCRIPT_DIR%launch-vsdevcmd.bat"

if /I "%WEBGPU_BACKEND%"=="DAWN" goto :configure_dawn

echo [BUILD] WGPU backend
cmake -S "%EXAMPLE_SOURCE_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
    -DWEBGPU_BACKEND=WGPU ^
    -DWEBGPU_BUILD_FROM_SOURCE=OFF
goto :configure_done

:configure_dawn
set DAWN_FROM_SOURCE=OFF
if /I "%DAWN_MODE%"=="SOURCE" set DAWN_FROM_SOURCE=ON
echo [BUILD] Dawn backend
cmake -S "%EXAMPLE_SOURCE_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
    -DWEBGPU_BACKEND=DAWN ^
    -DWEBGPU_BUILD_FROM_SOURCE=%DAWN_FROM_SOURCE%

:configure_done
if errorlevel 1 goto :configure_failed

echo [BUILD] Starting build...
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :build_failed
goto :success

:build_emdawn
echo [BUILD] emdawnwebgpu build
call emcmake cmake -S "%EXAMPLE_SOURCE_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
    -DWEBGPU_BACKEND=EMDAWN
if errorlevel 1 goto :configure_failed

cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :build_failed
goto :success

:configure_failed
echo [ERROR] CMake configuration failed.
exit /b 1

:build_failed
echo [ERROR] Build failed.
exit /b 1

:success
echo.
echo [SUCCESS] Example '%EXAMPLE_NAME%' built successfully!
echo.
echo Executable location: %BUILD_DIR%
if /I "%WEBGPU_BACKEND%"=="EMDAWN" (
    echo To run:
    echo   Start a web server in project root: python -m http.server 8080
    echo   Then open: http://localhost:8080/examples/build/%EXAMPLE_NAME%/%PROFILE_HOST%/%BUILDTYPE%/
)

endlocal
