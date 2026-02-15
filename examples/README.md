# Vienna WebGPU Engine - Examples

This directory contains example applications demonstrating the Vienna WebGPU Engine library.

Each example is a standalone project with its own CMakeLists.txt, showing how you would use the engine in a real-world scenario.

## Building Examples

### Quick Build (Default: main_demo, Debug, WGPU)

```bash
# From repository root
scripts/build-example.bat main_demo

# Or build with specific configuration
scripts/build-example.bat main_demo Debug WGPU
```

### Build Specific Example

```bash
# Syntax: scripts/build-example.bat [example_name] [Debug|Release] [WGPU|DAWN|Emscripten]

# Build main_demo in Debug with WGPU (default)
scripts/build-example.bat main_demo

# Build scene_editor in Release with WGPU
scripts/build-example.bat scene_editor Release WGPU

# Build multi_view in Debug
scripts/build-example.bat multi_view Debug WGPU
```

### Available Examples List

If you try to build a non-existent example, the script will show available examples:
```bash
scripts/build-example.bat nonexistent
# Shows list of all available examples
```

## Available Examples

### main_demo
The main comprehensive demo showcasing:
- Scene graph system with hierarchical transforms
- Orbit camera controls
- Model loading (OBJ format)
- Material system with PBR textures
- Multiple light types (ambient, directional, point, spot)
- Shadow mapping
- Day-night cycle system
- ImGui debug interface

**Location:** `examples/main_demo/main.cpp`  
**Build:** `scripts/build-example.bat main_demo`

### scene_editor
Scene editor example (work in progress).

**Location:** `examples/scene_editor/main.cpp`  
**Build:** `scripts/build-example.bat scene_editor`

### multi_view
Multiple viewport rendering example.

**Location:** `examples/multi_view/main.cpp`  
**Build:** `scripts/build-example.bat multi_view`

## Output

Built examples will be located in their respective build directories:
- Windows: `examples/[example_name]/build/Windows/[Debug|Release]/`
- Emscripten: `examples/[example_name]/build/Emscripten/[Debug|Release]/`

## Running Examples:
```
examples/build/[example_name]/[Platform]/[Config]/
``` from the build directory:
```bash
# Run from repository root
examples\build\main_demo\Windows\Debug\MainDemo.exe

# Or navigate to build directory
cd examples\build\main_demo\Windows\Debug
MainDemo.exe
```

### Emscripten (Not Currently Supported)
Emscripten builds are not yet functional. Future usage:
```bash
cd examples\build\main_demo
# Or navigate to the build directory
cd main_demo\build\Windows\Debug
MainDemo.exe
```

### Emscripten
Start a local server from the example's build directory:
```bash
cd mREADME.md
├── build/                   # Build output for all examples (gitignored)
│   ├── main_demo/
│   │   └── Windows/
│   │       ├── Debug/
│   │       └── Release/
│   ├── scene_editor/
│   └── multi_view/
├── main_demo/
│   ├── CMakeLists.txt       # Standalone CMake for this example
│   ├── main.cpp
│   ├── OrbitCamera.h
│   ├── OrbitCamera.cpp
│   ├── MainDemoImGuiUI.h
│   ├── MainDemoImGuiUI.cpp
│   └── DayNightCycle.h
├── scene_editor/
│   ├── CMakeLists.txt
│   └── main.cpp
└── multi_view/
    ├── CMakeLists.txt
    └── main.cpppp
│   └── build/
└── scene_editor/
    ├── CMakeLists.txt
    ├── main.cpp
    └── build/
```

## **Create directory:** `examples/my_example/`
2. **Add your source:** `main.cpp` and any additional files
3. **Create CMakeLists.txt:**

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
add_executable(MyExample 
    main.cpp
    # Add other source files here
)

# Link against the engine
target_link_libraries(MyExample PRIVATE WebGPU_Engine_Lib)

# Configure using engine's helper function
configure_engine_executable(MyExample)
```

4. **Build it:**
```bash
scripts/build-example.bat my_example
```

5. **Optional: Add assets**
   - Create `examples/my_example/assets/` directory
   - Add models, textures, etc.
   - Release builds will automatically copy to output

### What configure_engine_executable Does

The `configure_engine_executable` function automatically handles:
- **Windows:** Copies DLLs (SDL2, wgpu_native) to output directory
- **Release builds:** Copies engine resources and example assets
- **Debug builds:** Uses source directories directly (no copying for fast iteration)
- **Emscripten:** Configures shell file, preloads resources, source maps
- **Debugger setup:** Sets VS debugger environment variables
- WebGPU binaries
- Emscripten configuration
- Resource copying
- Debugger environment setup

### Custom Shell File (Emscripten)

To use a custom shell file:

```cmake
configure_engine_executable(MyExample SHELL_FILE "path/to/custom.html")
```
