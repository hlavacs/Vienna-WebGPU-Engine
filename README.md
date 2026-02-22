# Vienna-WebGPU-Engine

> **Version:** v0.3-alpha | **Status:** Active Development

A **cross-platform, WebGPU-based game engine** designed for educational purposes. Built with modern graphics APIs, it provides hands-on learning in game engine development using the WebGPU standard.

## Platform Support

| Platform | Status | Compiler | Notes |
|----------|--------|----------|-------|
| **Windows** | ✅ Working | MSVC 2019+ | Primary development platform |
| **macOS** | ✅ Working | Clang (Xcode CLT) | Tested on Apple Silicon |
| **Linux** | ✅ Working | Clang | Tested on Arch Linux Hyperland |
| **Web** | 🚧 In Progress | Emscripten | Build system exists, not functional |

*Working = Actively developed and tested. Untested = May require fixes.*

## Features

- ✨ Real-time WebGPU rendering with [wgpu-native](https://github.com/gfx-rs/wgpu-native)
- 🎨 PBR materials with cascaded shadow mapping
- 🌳 Scene graph with hierarchical transforms
- 🔄 Resource hot-reloading
- 📦 Factory pattern for GPU resource management
- 📐 Model loading: OBJ (stable), GLTF/GLB (WIP)
- 📚 Tutorial series (4 tutorials: shaders, bind groups, shadows (WIP), post-processing)

## Quick Start

### Prerequisites

| Tool | Windows | macOS | Linux |
|------|---------|-------|-------|
| **CMake 3.15+** | [Download](https://cmake.org/download/) | `brew install cmake` | `sudo apt install cmake` |
| **Ninja** | [Download](https://github.com/ninja-build/ninja/releases) | `brew install ninja` | `sudo apt install ninja-build` |
| **C++17 Compiler** | [Visual Studio 2019+](https://visualstudio.microsoft.com/) or [Build Tools](https://aka.ms/vs/stable/vs_BuildTools.exe) | `xcode-select --install` | `sudo apt install build-essential` |
| **Python 3** (for emscripten) | [Download](https://www.python.org/downloads/) | Pre-installed | `sudo apt install python3` |

### Installation
```bash
# 1. Clone repository
git clone https://github.com/hlavacs/Vienna-WebGPU-Engine.git
cd Vienna-WebGPU-Engine

# 2. Initialize submodules
git submodule update --init --recursive

# 3. Install Python dependencies (optional)
pip install -r requirements.txt
```

> Note: If you are building on Linux or WSL, make sure your repository uses LF line endings.
> It is recommended to configure Git before cloning:
>
>     git config --global core.autocrlf input
>
> If you already cloned the repository and encounter "cannot execute: required file not found",
> you can fix the scripts with:
>
>     sudo apt install dos2unix
>     dos2unix scripts/*.sh

## Building

### Option 1: VS Code (Recommended)

1. Open project in VS Code
2. Press `Ctrl+Shift+B` (Windows/Linux) or `⌘+Shift+B` (macOS)
3. Select a build task:
   - **Build Example: Main Demo (Debug)**
   - **Build Example: Tutorial (Debug)**
4. Press `F5` to build and debug (You may have to select the correct project)

### Option 2: Command Line

**Windows:**
```bat
scripts\build-example.bat main_demo Debug WGPU
scripts\build-example.bat tutorial Debug WGPU
```

**macOS/Linux:**
```bash
# First time only: make build scripts executable
chmod +x scripts/build-example.sh

# Then build
./scripts/build-example.sh main_demo Debug WGPU
./scripts/build-example.sh tutorial Debug WGPU
```

**Output:**
- **Windows:** `examples/build/<name>/Windows/Debug/`
- **macOS:** `examples/build/<name>/Mac/Debug/`
- **Linux:** `examples/build/<name>/Linux/Debug/`

### Option 3: IDE Setup

**Visual Studio (Windows):**
```bat
cmake -S . -B build_vs -G "Visual Studio 17 2022" -A x64
start build_vs\WebGPU_Engine.sln
```
Set example as startup project, enable C++ exceptions (`Ctrl+Alt+E`), press `F5`.

**Xcode (macOS):**
```bash
cmake -S . -B build_xcode -G Xcode
open build_xcode/WebGPU_Engine.xcodeproj
```
Select example scheme, press `⌘+R` to run.

## Documentation

### Getting Started
- **[Getting Started Guide](doc/GettingStarted.md)** - Build your first application
- **[Tutorial Series](doc/tutorials/01_unlit_shader.md)** - 4-part hands-on tutorials:
  1. Custom Shaders
  2. Bind Groups & Uniforms
  3. Shadow Mapping (WIP)
  4. Post-Processing Effects

### Technical Reference
- **[Engine Architecture](doc/EngineArchitecture.md)** - Design patterns and systems
- **[Bind Group System](doc/BindGroupSystem.md)** - Rendering pipeline details
- **[Core Principles](doc/CorePrinciples.md)** - Design philosophy and best practices

## Path Management

The engine uses `PathProvider` for cross-platform path resolution:
```cpp
// ✅ Application assets (example/assets/)
auto texture = PathProvider::getTextures("brick.png");
auto model = PathProvider::getModels("character.obj");

// ✅ Engine resources (engine/resources/)
auto shader = PathProvider::getResource("PBR_Lit_Shader.wgsl");

// ❌ Never use hardcoded paths
auto bad = "E:/Project/resources/texture.png";  // DON'T!
```

| Type | Debug | Release |
|------|-------|---------|
| **App Assets** | `<example>/assets/` | `<exe>/assets/` |
| **Engine Resources** | `<lib_project>/resources/` | `<dll>/resources/` |

See [CorePrinciples.md](doc/CorePrinciples.md#0-path-management) for details.

## Project Structure
```
Vienna-WebGPU-Engine/
├── include/engine/        # Public API headers
│   ├── core/              # Core systems (Handle, Identifiable, Versioned)
│   ├── resources/         # Resource management (Image, Material, Mesh)
│   ├── rendering/         # Rendering pipeline (Renderer, RenderPass)
│   ├── scene/             # Scene graph (Entity, Transform, Camera)
│   └── input/             # Input handling
├── src/engine/            # Engine implementation
├── resources/             # Engine shaders and built-in assets
├── examples/              # Example applications
│   ├── main_demo/         # Full-featured demo
│   ├── tutorial/          # Tutorial project
│   ├── multi_view/        # Multi-viewport demo
│   └── scene_editor/      # Scene editor (WIP)
├── external/              # Third-party dependencies
├── doc/                   # Documentation
└── scripts/               # Build automation
```

## Recommended VS Code Extensions

- **[WGSL](https://marketplace.visualstudio.com/items?itemName=noah-labrecque.wgsl-lang)** - Shader syntax highlighting
- **[CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)** - CMake integration
- **[C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)** - IntelliSense and debugging
- **[C/C++ Extension Pack](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack)** - Complete C++ toolset

## Known Limitations

- **Web Support:** Build system in progress, not yet functional
- **Model Loading:** GLTF/GLB support is work-in-progress; use OBJ for now
- **Scene Editor:** WIP

## Troubleshooting

### Build Issues

**"CMake not found"**
```bash
# Verify installation
cmake --version

# Add to PATH or reinstall (see Prerequisites)
```

**"Ninja not found"**
```bash
# Verify installation
ninja --version

# Install via package manager (see Prerequisites)
```

**"Visual Studio not found" (Windows)**
- Install [Visual Studio Build Tools](https://aka.ms/vs/stable/vs_BuildTools.exe)
- Select "Desktop development with C++"
- Restart terminal after installation

**"Xcode Command Line Tools not found" (macOS)**
```bash
xcode-select --install
# Follow prompts, then restart terminal
```

### Linux Graphics Issues

**SDL Wayland/X11 Errors**

If you encounter errors related to SDL video drivers or surface creation on Linux:

1. **Install SDL2 with video driver support:**
```bash
# Ubuntu/Debian
sudo apt-get install libsdl2-dev libwayland-dev libx11-dev

# Fedora
sudo dnf install SDL2-devel wayland-devel libX11-devel

# Arch
sudo pacman -S sdl2 wayland libx11
```

2. **If issues persist, force X11 mode:**
```bash
export SDL_VIDEODRIVER=x11
export DISPLAY=:0
./examples/build/tutorial/Linux/Debug/Tutorial
```

This is particularly useful on Wayland systems where SDL may not have proper Wayland support compiled in.

## Contributing

Contributions welcome! This is an educational project:

- **Documentation** - Help learners understand better
- **Features** - Add rendering capabilities
- **Examples** - Demonstrate engine usage
- **Bug Fixes** - Improve stability

Please read [Engine Architecture](doc/EngineArchitecture.md) and test on your platform before submitting PRs.

## License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Based in part on the [Learn WebGPU for C++](https://github.com/eliemichel/LearnWebGPU) tutorial by **Elie Michel**
  - Significant modifications, refactoring, and extensions have been made for this project
- Uses [wgpu-native v0.19.4.1](https://github.com/gfx-rs/wgpu-native) for native WebGPU implementation
- Built with [SDL2](https://www.libsdl.org/), [GLM](https://github.com/g-truc/glm), [ImGui](https://github.com/ocornut/imgui), and more

---

**Happy Learning and Building! 🚀**
