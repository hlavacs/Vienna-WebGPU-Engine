# Vienna-WebGPU-Engine

Vienna-WebGPU-Engine is a WebGPU-based game engine designed for educational purposes. It is built with modern graphics APIs to provide a hands-on learning experience in game engine development, using the WebGPU API. The project is still under development, and features will be added progressively as it matures.

## Features (Under Development)

- Real-time graphics rendering using WebGPU
- Cross-platform support (Windows, Web)
- Educational resources and examples to help you learn about game engine architecture and modern rendering techniques


## Getting Started

The Project uses the [**wgpu-native** v0.19.4.1]() 

### Prerequisites

Ensure you have the following installed on your system:

- [CMake](https://cmake.org/download/)
- [Ninja](https://ninja-build.org/) (optional, but recommended for building)
- [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) Version **4.0.6** is required for emscripten build. Can be installed 
- [http-server](https://www.npmjs.com/package/http-server) for emscripten debuging.

You may use `pip install -r requirements.txt`. It will install the versions used while development. Other versions may work as well.

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

3. **Install the Prerequisites**
   
   ```shell
   pip install -r requirements.txt
   ```

## Building the Project

### Building WebGPU Native

Use the provided VS Code tasks or launch settings.

Alternatively build manually:

```shell
cmake -S . -B ./bin/Windows -DCMAKE_BUILD_TYPE=Debug -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF
cmake --build ./bin/Windows --config Debug

cmake -S . -B ./bin/Windows -DCMAKE_BUILD_TYPE=Release -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF
cmake --build ./bin/Windows --config Release
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

### Building WebGPU Dawn

ToDo


### Building WebGPU for Web

**Required:** [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) Version **4.0.6** is required for emscripten build. Can be installed based on the given tutorial on the download side. <br>
Use the provided VS Code tasks or launch settings. 

Alternatively build manually:

```shell
emcmake cmake -B bin/Emscripten/Release
cmake --build bin/Emscripten/Release
```

And Run using:

```shell
python -m http.server 8080 -d bin/Emscripten/Release
```

### Recommended Extensions:

- [WGSL-Lang](https://marketplace.visualstudio.com/items/?itemName=noah-labrecque.wgsl-lang)
- [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
- [Cpp Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
- [WebAssembly DWARF Debugging](https://marketplace.visualstudio.com/items?itemName=ms-vscode.wasm-dwarf-debugging) (Debugging in Browser for Emscripten build)
- [Live Preview](https://marketplace.visualstudio.com/items?itemName=ms-vscode.live-server) (Debugging in Browser for Emscripten build)

### Contributing

Feel free to contribute to the project by forking the repository and submitting pull requests. Contributions are welcome, especially in areas such as optimization, feature development, and educational content.

### License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

This project is based in part on the excellent [Learn WebGPU for C++](https://github.com/eliemichel/LearnWebGPU) tutorial by Elie Michel.
Significant modifications, refactoring, and extensions have been made for this project.
Original code © 2022-2024 Elie Michel, MIT License.


