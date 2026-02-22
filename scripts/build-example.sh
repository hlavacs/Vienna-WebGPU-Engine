#!/bin/bash
set -e

EXAMPLE_NAME=${1:-main_demo}
BUILDTYPE=${2:-Debug}
WEBGPU_BACKEND=${3:-WGPU}
COMPILER=${4:-clang}

if [[ "$OSTYPE" == "darwin"* ]]; then
    PROFILE_HOST=Mac
else
    PROFILE_HOST=Linux
fi

if [[ ! "$WEBGPU_BACKEND" =~ ^(WGPU|DAWN|Emscripten)$ ]]; then
    echo "Invalid WEBGPU_BACKEND: $WEBGPU_BACKEND"
    echo "Must be WGPU, DAWN or Emscripten"
    exit 1
fi

if [[ "$COMPILER" == "gcc" ]]; then
    export CC=gcc
    export CXX=g++
elif [[ "$COMPILER" == "clang" ]]; then
    export CC=clang
    export CXX=clang++
else
    export CC="$COMPILER"
    export CXX="${COMPILER}++"
fi

if [[ "$WEBGPU_BACKEND" == "Emscripten" ]]; then
    PROFILE_HOST=Emscripten
fi

# Auto-detect SDL video driver on Linux
if [[ "$PROFILE_HOST" == "Linux" ]]; then
    if [[ -n "$WAYLAND_DISPLAY" ]]; then
        export SDL_VIDEODRIVER=wayland
        echo "[INFO] Wayland detected, setting SDL_VIDEODRIVER=wayland"
    elif [[ -n "$DISPLAY" ]]; then
        export SDL_VIDEODRIVER=x11
        echo "[INFO] X11 detected, setting SDL_VIDEODRIVER=x11"
    else
        echo "[WARN] Neither WAYLAND_DISPLAY nor DISPLAY is set, SDL video driver not forced"
    fi
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXAMPLES_DIR="$PROJECT_ROOT/examples"
EXAMPLE_SOURCE_DIR="$EXAMPLES_DIR/$EXAMPLE_NAME"
BUILD_DIR="$EXAMPLES_DIR/build/$EXAMPLE_NAME/$PROFILE_HOST/$BUILDTYPE"

if [[ ! -f "$EXAMPLE_SOURCE_DIR/CMakeLists.txt" ]]; then
    echo "[ERROR] Example '$EXAMPLE_NAME' not found or has no CMakeLists.txt"
    echo ""
    echo "Available examples:"
    for dir in "$EXAMPLES_DIR"/*; do
        if [[ -f "$dir/CMakeLists.txt" ]]; then
            echo "  - $(basename "$dir")"
        fi
    done
    exit 1
fi

echo "============================================"
echo "Building Example: $EXAMPLE_NAME"
echo "Build Type: $BUILDTYPE"
echo "Backend: $WEBGPU_BACKEND"
echo "Compiler: $CC / $CXX"
echo "Source Directory: $EXAMPLE_SOURCE_DIR"
echo "Build Directory: $BUILD_DIR"
if [[ -n "$SDL_VIDEODRIVER" ]]; then
    echo "SDL Video Driver: $SDL_VIDEODRIVER"
fi
echo "============================================"
echo ""

if [[ "$WEBGPU_BACKEND" == "Emscripten" ]]; then
    echo "[BUILD] Emscripten build"
    emcmake cmake -S "$EXAMPLE_SOURCE_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILDTYPE"
    if [[ $? -ne 0 ]]; then
        echo "[ERROR] CMake configuration failed."
        exit 1
    fi

    cmake --build "$BUILD_DIR"
    if [[ $? -ne 0 ]]; then
        echo "[ERROR] Build failed."
        exit 1
    fi
else
    if [[ "$WEBGPU_BACKEND" == "DAWN" ]]; then
        echo "[BUILD] Dawn backend"
        cmake -S "$EXAMPLE_SOURCE_DIR" -B "$BUILD_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE="$BUILDTYPE" \
            -DWEBGPU_BACKEND=DAWN \
            -DWEBGPU_BUILD_FROM_SOURCE=ON
    else
        echo "[BUILD] WGPU backend"
        cmake -S "$EXAMPLE_SOURCE_DIR" -B "$BUILD_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE="$BUILDTYPE" \
            -DWEBGPU_BACKEND=WGPU \
            -DWEBGPU_BUILD_FROM_SOURCE=OFF
    fi

    if [[ $? -ne 0 ]]; then
        echo "[ERROR] CMake configuration failed."
        exit 1
    fi

    echo "[BUILD] Starting build..."
    cmake --build "$BUILD_DIR"
    if [[ $? -ne 0 ]]; then
        echo "[ERROR] Build failed."
        exit 1
    fi
fi

echo ""
echo "[SUCCESS] Example '$EXAMPLE_NAME' built successfully!"
echo ""
echo "Executable location: $BUILD_DIR"
if [[ "$WEBGPU_BACKEND" == "Emscripten" ]]; then
    echo ""
    echo "To run:"
    echo "  Start a web server in project root: python -m http.server 8080"
    echo "  Then open: http://localhost:8080/examples/build/$EXAMPLE_NAME/$PROFILE_HOST/$BUILDTYPE/"
fi