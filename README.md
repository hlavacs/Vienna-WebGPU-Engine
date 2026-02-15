# Vienna-WebGPU-Engine

Vienna-WebGPU-Engine is a **cross-platform, WebGPU-based game engine** designed for educational purposes. Built with modern graphics APIs, it provides a hands-on learning experience in game engine development using the WebGPU API.

**Current Platform Support:** Windows (Native WebGPU via wgpu-native)  
**Status:** Active development - features are being added progressively

## Features

- ✅ Real-time graphics rendering using WebGPU (wgpu-native)
- ✅ Cross-platform architecture (Windows native, Web support planned)
- ✅ Modern rendering pipeline with shadow mapping and PBR materials
- ✅ Scene graph system with hierarchical transforms
- ✅ Resource management with hot-reloading support
- ✅ Factory pattern for GPU resource creation
- ✅ Model loading: OBJ (stable), GLTF/GLB (work in progress)
- ✅ Educational resources and comprehensive documentation

**Note:** Emscripten/Web support is **not currently available** and is planned for future releases.


## Getting Started

### Prerequisites

Ensure you have the following installed:

- **[CMake](https://cmake.org/download/)** (3.15+)
- **[Ninja](https://ninja-build.org/)** (recommended build system)
- **C++17 compatible compiler** (MSVC 2019+, GCC 9+, Clang 10+)
- **Python 3.x** (for build scripts)

The engine uses **[wgpu-native v0.19.4.1](https://github.com/gfx-rs/wgpu-native)** as the WebGPU implementation.

### Optional Tools

- **[Visual Studio 2022](https://visualstudio.microsoft.com/)** - For native debugging and crash analysis
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

- **[Getting Started Guide](doc/GettingStarted.md)** - Learn how to use the engine for your own projects
- **[Engine Architecture](doc/EngineArchitecture.md)** - Technical architecture and design patterns
- **[Bind Group System](doc/BindGroupSystem.md)** - Advanced rendering system details
- **[Core Principles](doc/CorePrinciples.md)** - Engine design philosophy
- **[Building Dawn](doc/BuildingDawn.md)** - Instructions for building with Dawn backend (experimental)

## Building the Project

### Windows Native (wgpu-native)

**Recommended:** Use VS Code tasks or the provided build scripts.

#### Using Build Scripts

```shell
# Debug build
scripts/build.bat Debug WGPU

# Release build
scripts/build.bat Release WGPU
```

#### Manual CMake Build

```shell
# Debug
cmake -S . -B build/Windows/Debug -DCMAKE_BUILD_TYPE=Debug -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF -G Ninja
cmake --build build/Windows/Debug

# Release
cmake -S . -B build/Windows/Release -DCMAKE_BUILD_TYPE=Release -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF -G Ninja
cmake --build build/Windows/Release
```

#### Generating Visual Studio Solution for Debugging

To debug in Visual Studio (recommended for catching crashes and low-level debugging):

```shell
# Generate Visual Studio solution for the entire project (includes all examples)
cmake -S . -B build_vs -G "Visual Studio 17 2022" -A x64 -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF -DBUILD_EXAMPLES=ON

# Open the solution in Visual Studio
start build_vs/WebGPU_Engine.sln
```

Once in Visual Studio:
1. In Solution Explorer, find the example you want to debug (e.g., **MainDemo**)
2. Right-click the example project → **Set as Startup Project**
3. Set configuration to **Debug | x64** (top toolbar)
4. Enable exception breaking: **Debug → Windows → Exception Settings** (Ctrl+Alt+E), check **C++ Exceptions** and **Win32 Exceptions**
5. Press **F5** to start debugging

This is especially useful for debugging crashes that occur before `main()` or during static initialization.

**Note:** The Visual Studio solution includes the engine library and all dependencies (SDL2, WebGPU, etc.), so everything is built together.

### Building Examples

```shell
# Build specific example
scripts/build-example.bat main_demo Debug WGPU
scripts/build-example.bat scene_editor Debug WGPU

# Build with defaults (main_demo, Debug, WGPU)
scripts/build-example.bat main_demo
```

### Web Build (Emscripten) - Not Currently Supported

**Status:** Web builds are currently non-functional and under development.

Emscripten support is planned for future releases. The build infrastructure exists but requires significant work to be operational.

<details>
<summary>Experimental Emscripten Build (Non-Functional)</summary>

```shell
# Requires Emscripten 4.0.6
emcmake cmake -B build/Emscripten/Release
cmake --build build/Emscripten/Release

# Run local server
python -m http.server 8080 -d build/Emscripten/Release
```

**Warning:** This build is currently broken and not supported.
</details>

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
│   └── scene_editor/      # Scene editor (WIP)
├── external/              # Third-party libraries
├── doc/                   # Documentation
└── scripts/               # Build scripts
```

## Getting Help

- **Start here:** Read the **[Getting Started Guide](doc/GettingStarted.md)** to build your first application
- Check the **[Engine Architecture](doc/EngineArchitecture.md)** for technical details
- Review the **[copilot-instructions.md](.github/copilot-instructions.md)** for detailed implementation guidance
- Explore the **examples/** folder for usage patterns
- Look at inline documentation in header files

## Contributing

Contributions are welcome! This is an educational project, so contributions in the following areas are especially appreciated:

- **Documentation improvements** - Help make the engine more accessible to learners
- **Feature development** - Implement new rendering features or engine capabilities
- **Bug fixes** - Help improve stability and correctness
- **Examples** - Create new example applications demonstrating engine features
- **Optimization** - Performance improvements and profiling

**Before contributing:**
1. Read the **[Engine Architecture](doc/EngineArchitecture.md)** documentation
2. Follow the coding conventions outlined in the documentation
3. Ensure your code builds successfully on Windows
4. Add appropriate documentation for new features

Fork the repository and submit pull requests with clear descriptions of your changes.

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


