# Vienna-WebGPU-Engine

> **Version:** v0.1-alpha | **Status:** Active Development

Vienna-WebGPU-Engine is a **cross-platform, WebGPU-based game engine** designed for educational purposes. Built with modern graphics APIs, it provides a hands-on learning experience in game engine development using the WebGPU API.

**Current Platform Support:** Windows (Native WebGPU via wgpu-native)  
**Note:** Emscripten/Web support planned for future releases

## Features

- ✅ Real-time WebGPU rendering (wgpu-native)
- ✅ PBR materials with shadow mapping
- ✅ Scene graph with hierarchical transforms
- ✅ Resource management with hot-reloading
- ✅ Factory pattern for GPU resources
- ✅ Model loading: OBJ (stable), GLTF/GLB (WIP)
- ✅ Comprehensive documentation
- ✅ Tutorial series (4 tutorials: shaders, bind groups, shadows, post-processing)


## Getting Started

### Prerequisites

Ensure you have the following installed:

- **[CMake](https://cmake.org/download/)** (3.15+)
- **[Ninja](https://ninja-build.org/)** (recommended build system)
- **C++17 compatible compiler** (MSVC 2019+, GCC 9+, Clang 10+)
  - **Windows:** MSVC Build Tools recommended. Install via:
    - [Visual Studio](https://visualstudio.microsoft.com/) (includes Build Tools), or
    - [Visual Studio Build Tools](https://aka.ms/vs/stable/vs_BuildTools.exe) (standalone) from [visualstudio.microsoft.com/downloads](https://visualstudio.microsoft.com/de/downloads/?q=build+tools)
- **Python 3.x** (for build scripts)

The engine uses **[wgpu-native v0.19.4.1](https://github.com/gfx-rs/wgpu-native)** as the WebGPU implementation.

### Optional Tools

- **[Visual Studio Code](https://code.visualstudio.com/)** - Recommended for native debugging and crash analysis
- **[Emscripten 4.0.6](https://emscripten.org/)** - Web support (not yet functional)
- **[http-server](https://www.npmjs.com/package/http-server)** - For web builds (future use)

## Setup

1. **Clone the repository:**
   
   ```shell
   git clone https://github.com/hlavacs/Vienna-WebGPU-Engine.git
   cd Vienna-WebGPU-Engine
   ```

2. **Initialize submodules:**
   
   ```shell
   git submodule update --init --recursive
   ```

3. **Install Python dependencies (optional):**
   
   ```shell
   pip install -r requirements.txt
   ```

## Documentation

### Getting Started
- **[Getting Started Guide](doc/GettingStarted.md)** - Build your first application
- **[Tutorial Series (4 Parts)](doc/tutorials/01_unlit_shader.md)** - Learn shaders, bind groups, shadows (WIP), and post-processing

### Technical Documentation
- **[Engine Architecture](doc/EngineArchitecture.md)** - Design patterns and architecture
- **[Bind Group System](doc/BindGroupSystem.md)** - Rendering system details
- **[Core Principles](doc/CorePrinciples.md)** - Design philosophy and data flow

## Path Management

The engine uses two separate path systems via `PathProvider`:

| Type | Debug | Release | API |
|------|-------|---------|-----|
| **Application Assets** | `<example>/assets/` | `<exe>/assets/` | `getTextures()`, `getModels()` |
| **Engine Resources** | `<project>/resources/` | `<dll>/resources/` | `getResource()` |

**Always use `PathProvider` for paths:**
```cpp
// ✅ Correct
auto appTexture = PathProvider::getTextures("brick.png");
auto engineShader = PathProvider::getResource("PBR_Lit_Shader.wgsl");

// ❌ Wrong
auto path = "E:/Project/resources/texture.png";
```

See [CorePrinciples.md](doc/CorePrinciples.md#0-path-management) for details.

## Known Limitations

- **Web Support:** Emscripten/WebAssembly support is planned but not yet functional
- **Platform Coverage:** Windows only (Linux/macOS support planned might work with modifications)
- **Model Format:** GLTF/GLB loading is work-in-progress; OBJ fully supported
- **Editor:** Scene editor (WIP) not yet feature-complete

## Building the Project

### Quick Start with VS Code (Recommended)

Press `Ctrl+Shift+B` to select a build task:
- `Build Windows (Debug/Release)` - Build engine
- `Build Example: Main Demo (Debug)` - Build main demo
- `Build Example: Tutorial (Debug)` - Build tutorial

Press `F5` to build and run with debugger.

### Command Line

```shell
# Build engine
scripts/build.bat Debug WGPU

# Build example
scripts/build-example.bat main_demo Debug WGPU
scripts/build-example.bat tutorial Debug WGPU
```

**Output:** `build/Windows/Debug/` (engine), `examples/build/<name>/Windows/Debug/` (examples)

### Visual Studio for Debugging

```shell
cmake -S . -B build_vs -G "Visual Studio 17 2022" -A x64 -DWEBGPU_BACKEND=WGPU
start build_vs/WebGPU_Engine.sln
```

Set your example as startup project, enable C++ exceptions (`Ctrl+Alt+E`), press `F5`.

### Recommended VS Code Extensions

- **[WGSL-Lang](https://marketplace.visualstudio.com/items?itemName=noah-labrecque.wgsl-lang)** - WGSL syntax highlighting and language support
- **[CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)** - CMake integration
- **[C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)** - C++ IntelliSense and debugging
- **[WebAssembly DWARF Debugging](https://marketplace.visualstudio.com/items?itemName=ms-vscode.wasm-dwarf-debugging)** - For future web debugging
- **[Live Preview](https://marketplace.visualstudio.com/items?itemName=ms-vscode.live-server)** - For future web builds

## Project Structure

```
Vienna-WebGPU-Engine/
├── include/engine/         # Engine headers
│   ├── core/              # Core utilities (Handle, Identifiable)
│   ├── resources/         # Resource management
│   ├── rendering/         # Rendering system
│   ├── scene/             # Scene graph
│   └── input/             # Input handling
├── src/engine/            # Engine implementation
├── resources/             # Shaders and assets
├── examples/              # Example applications
│   ├── main_demo/         # Main demo application
│   ├── multi_view/        # Mutli view demo application
│   └── scene_editor/      # Scene editor (WIP)
├── external/              # Third-party libraries
├── doc/                   # Documentation
└── scripts/               # Build scripts
```

## Getting Help

1. **[Getting Started Guide](doc/GettingStarted.md)** - Build your first app
2. **[Tutorials](doc/tutorials/01_unlit_shader.md)** - Tutorials
3. **[Engine Architecture](doc/EngineArchitecture.md)** - Technical reference
4. Browse **examples/** for usage patterns

## Contributing

Contributions welcome! This is an educational project—help make it better:

- **Documentation** - Improve accessibility for learners
- **Features** - Add rendering capabilities
- **Examples** - Demonstrate engine features
- **Bug fixes** - Improve stability

Read [Engine Architecture](doc/EngineArchitecture.md), follow coding conventions, ensure Windows builds succeed.

## License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

This project builds upon excellent work from the community:

- Based in part on the [Learn WebGPU for C++](https://github.com/eliemichel/LearnWebGPU) tutorial by **Elie Michel**
- Significant modifications, refactoring, and extensions have been made for this project
- Original code © 2022-2024 Elie Michel, MIT License
- Uses [wgpu-native](https://github.com/gfx-rs/wgpu-native) as the WebGPU implementation
- Leverages [SDL2](https://www.libsdl.org/), [GLM](https://github.com/g-truc/glm), [ImGui](https://github.com/ocornut/imgui), and other excellent libraries

---

**Happy Learning and Building! 🚀**


