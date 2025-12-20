@echo off
setlocal

:: Args: build type, backend, optional example name
set BUILDTYPE=%1
set WEBGPU_BACKEND=%2
set EXAMPLE_NAME=%3
set PROFILE_HOST=Windows

:: Defaults
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

:: Set the build directory
set BUILD_DIR=build\%PROFILE_HOST%\%BUILDTYPE%
set EXAMPLES_BUILD_DIR=%BUILD_DIR%\examples

echo ============================================
echo Building Examples
echo Build Type: %BUILDTYPE%
echo Backend: %WEBGPU_BACKEND%
if not "%EXAMPLE_NAME%"=="" (
    echo Target: %EXAMPLE_NAME%
)
echo ============================================

:: Configure and build examples based on the backend
if /I "%WEBGPU_BACKEND%"=="Emscripten" (
    echo [BUILD] Emscripten examples build
    emcmake cmake -S examples -B "%EXAMPLES_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE%
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed.
        exit /b 1
    )
    
    if not "%EXAMPLE_NAME%"=="" (
        cmake --build "%EXAMPLES_BUILD_DIR%" --target %EXAMPLE_NAME%
    ) else (
        cmake --build "%EXAMPLES_BUILD_DIR%" --parallel
    )
    if errorlevel 1 (
        echo [ERROR] Build failed.
        exit /b 1
    )
) else (
    call scripts\launch-vsdevcmd.bat
    
    if /I "%WEBGPU_BACKEND%"=="DAWN" (
        echo [BUILD] Dawn backend examples
        cmake -S examples -B "%EXAMPLES_BUILD_DIR%" -G Ninja ^
            -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
            -DWEBGPU_BACKEND=DAWN ^
            -DWEBGPU_BUILD_FROM_SOURCE=ON
    ) else (
        echo [BUILD] WGPU backend examples
        cmake -S examples -B "%EXAMPLES_BUILD_DIR%" -G Ninja ^
            -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
            -DWEBGPU_BACKEND=WGPU ^
            -DWEBGPU_BUILD_FROM_SOURCE=OFF
    )
    
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed.
        exit /b 1
    )
    
    :: Build specific example or all examples
    echo [BUILD] Starting build...
    if not "%EXAMPLE_NAME%"=="" (
        echo Building target: %EXAMPLE_NAME%
        cmake --build "%EXAMPLES_BUILD_DIR%" --target %EXAMPLE_NAME%
    ) else (
        echo Building all examples...
        cmake --build "%EXAMPLES_BUILD_DIR%" --parallel
    )
    if errorlevel 1 (
        echo [ERROR] Build failed.
        exit /b 1
    )
)

:: If we got here, build succeeded
echo [SUCCESS] Examples build completed successfully!
echo.
echo Built examples are in: %EXAMPLES_BUILD_DIR%

endlocal
