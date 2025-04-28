@echo off
setlocal


:: Args: build type, optional host profile, optional backend
set BUILDTYPE=%1
set WEBGPU_BACKEND=%2
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

:: Set the build profile and directory
set PROFILE_BUILD=profiles\Windows
set PROFILE_HOST_PATH=profiles\%PROFILE_HOST%
set BUILD_DIR=build\%PROFILE_HOST%\%BUILDTYPE%

:: Conan install
conan install . --build=missing -pr:b %PROFILE_BUILD% -pr:h %PROFILE_HOST_PATH% -s build_type=%BUILDTYPE% -c tools.cmake.cmaketoolchain:generator=Ninja

:: Configure project based on the host and backend
if /I "%WEBGPU_BACKEND%"=="Emscripten" (
    echo emcmake cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% && cmake --build "%BUILD_DIR%" --parallel
    emcmake cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% && cmake --build "%BUILD_DIR%" --parallel
) else (
    call scripts\launch-vsdevcmd.bat
    if /I "%WEBGPU_BACKEND%"=="DAWN" (
        echo cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% -DWEBGPU_BACKEND=DAWN -DWEBGPU_BUILD_FROM_SOURCE=ON
        cmake -S . -B "%BUILD_DIR%" -G Ninja ^
            -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
            -DWEBGPU_BACKEND=DAWN ^
            -DWEBGPU_BUILD_FROM_SOURCE=ON
    ) else (
        echo cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF
        cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILDTYPE% -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF
    )

    :: Build project
    echo cmake --build "%BUILD_DIR%" --parallel
    cmake --build "%BUILD_DIR%" --parallel

)
endlocal
