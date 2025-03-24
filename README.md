# Vienna-WebGPU-Engine

Vienna-WebGPU-Engine is a WebGPU-based game engine designed for educational purposes. It is built with modern graphics APIs to provide a hands-on learning experience in game engine development, focusing on the WebGPU API. The project is still under development, and features will be added progressively as it matures.

## Features (Under Development)

- Real-time graphics rendering using WebGPU
- Cross-platform support (Windows, Web)
- Educational resources and examples to help you learn about game engine architecture and modern rendering techniques

## Getting Started

### Prerequisites

Ensure you have the following installed on your system:

- [Conan2](https://docs.conan.io/2/reference/commands/install.html)
- [Cmake](https://cmake.org/download/)
- [Ninja](https://ninja-build.org/) (optional, but recommended for building)

You may use `pip install -r requirements.txt`. It will install the versions used while development. Other versions may work as well.

### Setup

1. **Clone the repository**:

2. **Install the Prerequisites**
    ```sh
    pip install -r requirements.txt
    ```

## Build

### Conan Install

To install the required dependencies using Conan, run:

```cmd
conan install . -s build_type=Debug --build=missing

conan install . -s build_type=Release --build=missing
```

### WebGPU Native build

Use the VSC launch settings provided or alterativly 

```cmd
cmake -S . -B ./bin/Windows -DCMAKE_BUILD_TYPE=Debug -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF
cmake --build ./bin/Windows --config Debug

cmake -S . -B ./bin/Windows -DCMAKE_BUILD_TYPE=Release -DWEBGPU_BACKEND=WGPU -DWEBGPU_BUILD_FROM_SOURCE=OFF
cmake --build ./bin/Windows --config Release
```

## Setup

1. **Clone the repository**:

2. **Install the Prerequisites**
    ```sh
    pip install -r requirements.txt
    ```

### Contributing
Feel free to contribute to the project by forking the repository and submitting pull requests. Contributions are welcome, especially in areas such as optimization, feature development, and educational content.

### License
This project is licensed under the MIT License - see the LICENSE file for details.

