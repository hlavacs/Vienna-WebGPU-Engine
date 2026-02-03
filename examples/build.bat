@echo off
setlocal

:: Args: example name (optional, defaults to main_demo), build type (optional, defaults to Debug), backend (optional, defaults to WGPU)
set EXAMPLE_NAME=%1
set BUILDTYPE=%2
set WEBGPU_BACKEND=%3
set PROFILE_HOST=Windows

:: Defaults
if "%EXAMPLE_NAME%"=="" set EXAMPLE_NAME=main_demo
if "%BUILDTYPE%"=="" set BUILDTYPE=Debug
if "%WEBGPU_BACKEND%"=="" set WEBGPU_BACKEND=WGPU

:: Validate WEBGPU_BACKEND
if /I not "%WEBGPU_BACKEND%"=="WGPU" if /I not "%WEBGPU_BACKEND%"=="DAWN" if /I not "%WEBGPU_BACKEND%"=="Emscripten" (
    echo Invalid WEBGPU_BACKEND: %WEBGPU_BACKEND%
    echo Must be WGPU, DAWN or Emscripten 
    exit /b 1
)

if /I "%WEBGPU_BACKEND%"=="Emscripten" (
    set PROFILE_HOST=Emscripten
)

:: Check if example exists
if not exist "%~dp0%EXAMPLE_NAME%\CMakeLists.txt" (
    echo [ERROR] Example '%EXAMPLE_NAME%' not found or has no CMakeLists.txt
    echo.
    echo Available examples:
    for /d %%d in ("%~dp0*") do (
        if exist "%%d\CMakeLists.txt" echo   - %%~nxd
    )
    exit /b 1
)

:: Set the build directory
set BUILD_DIR=build\%EXAMPLE_NAME%\%PROFILE_HOST%\%BUILDTYPE%

echo ============================================
echo Building Example: %EXAMPLE_NAME%
echo Build Type: %BUILDTYPE%
echo Backend: %WEBGPU_BACKEND%
echo Build Directory: %BUILD_DIR%
echo ============================================
echo.

:: Configure and build example based on the backend
if /I "%WEBGPU_BACKEND%"=="Emscripten" (
    echo [BUILD] Emscripten build
    emcmake cmake -S "%~dp0%EXAMPLE_NAME%" -B "%~dp0%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE%
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed.
        exit /b 1
    )
    
    cmake --build "%~dp0%BUILD_DIR%"
    if errorlevel 1 (
        echo [ERROR] Build failed.
        exit /b 1
    )
) else (
    call "%~dp0..\scripts\launch-vsdevcmd.bat"
    
    if /I "%WEBGPU_BACKEND%"=="DAWN" (
        echo [BUILD] Dawn backend
        cmake -S "%~dp0%EXAMPLE_NAME%" -B "%~dp0%BUILD_DIR%" -G Ninja ^
            -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
            -DWEBGPU_BACKEND=DAWN ^
            -DWEBGPU_BUILD_FROM_SOURCE=ON
    ) else (
        echo [BUILD] WGPU backend
        cmake -S "%~dp0%EXAMPLE_NAME%" -B "%~dp0%BUILD_DIR%" -G Ninja ^
            -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
            -DWEBGPU_BACKEND=WGPU ^
            -DWEBGPU_BUILD_FROM_SOURCE=OFF
    )
    
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed.
        exit /b 1
    )
    
    :: Build example
    echo [BUILD] Starting build...
    cmake --build "%~dp0%BUILD_DIR%"
    if errorlevel 1 (
        echo [ERROR] Build failed.
        exit /b 1
    )
)

:: If we got here, build succeeded
echo.
echo [SUCCESS] Example '%EXAMPLE_NAME%' built successfully!
echo.
echo Executable location: %BUILD_DIR%\
echo.
echo To run:
if /I "%WEBGPU_BACKEND%"=="Emscripten" (
    echo   Start a web server: python -m http.server 8080
    echo   Then open: http://localhost:8080/%BUILD_DIR%/
) else (
    echo   %BUILD_DIR%\%EXAMPLE_NAME%.exe
)

endlocal
