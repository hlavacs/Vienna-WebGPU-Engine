#!/bin/bash
set -e

# Args: build type (optional, defaults to Debug), backend (optional, defaults to WGPU)
BUILDTYPE=${1:-Debug}
WEBGPU_BACKEND=${2:-WGPU}
PROFILE_HOST=Linux

# Validate WEBGPU_BACKEND
if [[ ! "$WEBGPU_BACKEND" =~ ^(WGPU|DAWN|Emscripten)$ ]]; then
    echo "Invalid WEBGPU_BACKEND: $WEBGPU_BACKEND"
    echo "Must be WGPU, DAWN or Emscripten"
    exit 1
fi

if [[ "$WEBGPU_BACKEND" == "Emscripten" ]]; then
    PROFILE_HOST=Emscripten
fi

# Set the build profile and directory
PROFILE_BUILD=profiles/Linux
PROFILE_HOST_PATH=profiles/$PROFILE_HOST
BUILD_DIR=build/$PROFILE_HOST/$BUILDTYPE

# Conan install (commented out - adjust as needed)
# conan install . --build=missing -pr:b $PROFILE_BUILD -pr:h $PROFILE_HOST_PATH -s build_type=$BUILDTYPE -c tools.cmake.cmaketoolchain:generator=Ninja

# Configure project based on the host and backend
if [[ "$WEBGPU_BACKEND" == "Emscripten" ]]; then
    echo "[BUILD] Emscripten build"
    echo "emcmake cmake -S . -B \"$BUILD_DIR\" -G Ninja -DCMAKE_BUILD_TYPE=$BUILDTYPE"
    emcmake cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILDTYPE"
    if [[ $? -ne 0 ]]; then
        echo "[ERROR] CMake configuration failed."
        exit 1
    fi
    cmake --build "$BUILD_DIR" --parallel
    if [[ $? -ne 0 ]]; then
        echo "[ERROR] Build failed."
        exit 1
    fi
else
    if [[ "$WEBGPU_BACKEND" == "DAWN" ]]; then
        echo "[BUILD] Dawn backend"
        echo "cmake -S . -B \"$BUILD_DIR\" -G Ninja -DCMAKE_BUILD_TYPE=$BUILDTYPE -DWEBGPU_BACKEND=DAWN -DWEBGPU_BUILD_FROM_SOURCE=ON"
        cmake -S . -B "$BUILD_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE="$BUILDTYPE" \
            -DWEBGPU_BACKEND=DAWN \
            -DWEBGPU_BUILD_FROM_SOURCE=ON
    else
        echo "[BUILD] WGPU backend"
        echo "cmake -S . -B \"$BUILD_DIR\" -G Ninja -DCMAKE_BUILD_TYPE=$BUILDTYPE -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF"
        cmake -S . -B "$BUILD_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE="$BUILDTYPE" \
            -DWEBGPU_BACKEND=WGPU \
            -DWEBGPU_BUILD_FROM_SOURCE=OFF
    fi

    if [[ $? -ne 0 ]]; then
        echo "[ERROR] CMake configuration failed."
        exit 1
    fi

    # Build project
    echo "[BUILD] Starting build..."
    echo "cmake --build \"$BUILD_DIR\" --parallel"
    cmake --build "$BUILD_DIR" --parallel
    if [[ $? -ne 0 ]]; then
        echo "[ERROR] Build failed."
        exit 1
    fi
fi

# If we got here, build succeeded
echo "[SUCCESS] Build completed successfully!"
