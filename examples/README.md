# Vienna WebGPU Engine - Examples

This directory contains example applications demonstrating the Vienna WebGPU Engine library.

Each example is a standalone project with its own CMakeLists.txt, showing how you would use the engine in a real-world scenario.

## Building Examples

### Quick Build (Default: MainDemo)

```bash
# From examples/ directory, build main_demo in Debug mode with WGPU
cd examples
build.bat

# Or from root directory
cd examples && build.bat
```

### Build Specific Example

```bash
# Syntax: build.bat [example_name] [Debug|Release] [WGPU|DAWN|Emscripten]

# Build MainDemo in Debug with WGPU (default)
build.bat main_demo

# Build GameEngineExample in Release with WGPU
build.bat game_engine_example Release WGPU

# Build HelloWebGPU in Debug with Emscripten
build.bat hello-webgpu Debug Emscripten
```

## Available Examples

### MainDemo
The main comprehensive demo showcasing:
- Scene graph system
- Orbit camera controls
- Model loading (OBJ/glTF)
- Material system with PBR
- Multiple lights
- ImGui debug interface

**Location:** `examples/main_demo/main.cpp`

### GameEngineExample
Simple example showing basic GameEngine API usage:
- Engine initialization
- Scene creation
- Loading models
- Basic camera setup

**Location:** `examples/game_engine_example/main.cpp`

### HelloWebGPU
Minimal WebGPU example (if available).

**Location:** `examples/hello-webgpu/main.cpp`

### SceneEditor
Scene editor example (if available).

**Location:** `examples/scene_editor/main.cpp`

## Output

Built examples will be located in their respective build directories:
- Windows: `examples/[example_name]/build/Windows/[Debug|Release]/`
- Emscripten: `examples/[example_name]/build/Emscripten/[Debug|Release]/`

## Running Examples

### Windows
Simply run the executable:
```bash
# From examples directory
main_demo\build\Windows\Debug\MainDemo.exe

# Or navigate to the build directory
cd main_demo\build\Windows\Debug
MainDemo.exe
```

### Emscripten
Start a local server from the example's build directory:
```bash
cd main_demo\build\Emscripten\Debug
python -m http.server 8080
# Then open: http://localhost:8080/MainDemo.html
```

## Project Structure

```
examples/
├── build.bat                # Build script for examples
├── README.md
├── main_demo/
│   ├── CMakeLists.txt       # Standalone CMake for this example
│   ├── main.cpp
│   └── build/               # Build output (gitignored)
├── game_engine_example/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   └── build/
├── hello-webgpu/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   └── build/
└── scene_editor/
    ├── CMakeLists.txt
    ├── main.cpp
    └── build/
```

## Creating Your Own Example

Each example is a standalone project. To create a new one:

1. Create a new directory under `examples/` (e.g., `my_example/`)
2. Add your `main.cpp` file
3. Create a minimal `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyExample VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the Vienna WebGPU Engine library
if(NOT TARGET WebGPU_Engine_Lib)
    get_filename_component(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
    add_subdirectory(${ENGINE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/engine)
endif()

# Create executable
add_executable(MyExample main.cpp)

# Link against the engine
target_link_libraries(MyExample PRIVATE WebGPU_Engine_Lib)

# Configure using engine's helper function
configure_engine_executable(MyExample)
```

4. Build it:
```bash
cd examples
build.bat my_example
```

That's it! The `configure_engine_executable` function automatically handles:
- DLL copying on Windows
- WebGPU binaries
- Emscripten configuration
- Resource copying
- Debugger environment setup

### Custom Shell File (Emscripten)

To use a custom shell file:

```cmake
configure_engine_executable(MyExample SHELL_FILE "path/to/custom.html")
```
