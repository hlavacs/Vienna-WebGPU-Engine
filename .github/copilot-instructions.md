# Vienna-WebGPU-Engine Codebase Guide


This document provides essential knowledge to help AI coding assistants be productive when working with the Vienna-WebGPU-Engine codebase.

## Coding Style & Conventions

**Indentation:** Use tabs for indentation, not spaces. All C++ and shader files should consistently use tab-based formatting.

**Naming Conventions:** Use `CamelCase`

**File Structure:** Organize files by feature or module, with clear naming to indicate purpose. For namespaces use in style `engine::rendering::webgpu` not multiple `{namespace webgpu; namespace rendering; namespace engine; }`.

**Header Guards:** Use `#pragma once` for header files to prevent multiple inclusions.

**Include Order:** Use Blank Lines Between Groups
1. Header for implementation file
2. Standard library headers
3. Third-party library headers
4. Project-specific headers
5. Platform-specific / System Headers (optional)

**Include Paths:** Use relative paths for includes within the project, e.g., `#include "engine/rendering/webgpu/WebGPUContext.h"`.

**Function Length:** Keep functions short and focused. If a function exceeds 50 lines, consider refactoring it into smaller helper functions.

**Documentation:** Use Doxygen-style comments for public APIs and important classes. Include brief descriptions, parameter explanations, and return values.

## Architecture Overview

Vienna-WebGPU-Engine is an educational WebGPU-based rendering engine with cross-platform support for Windows and Web (via Emscripten). The engine follows a layered architecture:

1. **Core Layer**: Base classes like `Identifiable`, `Versioned`, and `Handle<T>` for resource management
2. **Resource Layer**: Resource management system with managers for textures, meshes, materials, and models
3. **Rendering Layer**: WebGPU-based rendering pipeline with abstractions for platform-specific details
4. **Application Layer**: Main application loop and integration points for the various systems

### Key Design Patterns

1. **Factory Pattern**: Used extensively with `WebGPUxxxFactory` classes that create GPU resources from CPU resources
2. **Resource Handles**: `Handle<T>` template for type-safe resource references with validation
3. **Manager Pattern**: Resource managers (`TextureManager`, `MeshManager`, etc.) for CPU-side resources
4. **Context Pattern**: `WebGPUContext` provides access to all WebGPU resources and factories

## Build System and Workflows

### Building the Project

```bash
# Windows Debug build with WGPU backend
scripts/build.bat Debug WGPU

# Windows Release build with WGPU backend
scripts/build.bat Release WGPU

# Emscripten Debug build
scripts/build.bat Debug Emscripten

# Emscripten Release build
scripts/build.bat Release Emscripten
```

### Running the Web Build

For Emscripten builds, start a local server:
```bash
# Start a development server
python -m http.server 8080
```

## Key Components and Their Relationships

### WebGPU Resource Management

1. **WebGPUContext**: Central hub with access to device, factories, and default resources
2. **WebGPUxxxFactory classes**: Create GPU resources from CPU resources
   - `WebGPUTextureFactory`, `WebGPUMaterialFactory`, `WebGPUMeshFactory`, etc.
3. **WebGPUBindGroupFactory**: Creates bind groups and layouts for shader resources
4. **WebGPUPipelineFactory**: Creates render pipelines with shader configuration

### Material System

1. **Material**: CPU-side material definition with properties and texture references
2. **WebGPUMaterial**: GPU-side representation with bind groups and WebGPU resources
3. **MaterialProperties**: Unified structure for material properties sent to shaders
4. **WebGPUMaterialTextures**: Container for material textures with named properties

### Resource Flow

CPU Resource (e.g., Material) → Resource Manager → WebGPUFactory → GPU Resource (e.g., WebGPUMaterial) → Renderer

## Common Patterns and Conventions

1. **Resource Cleanup**: WebGPU resources need explicit cleanup with `.release()`
2. **Factory Pattern**: All GPU resources are created through factory classes
3. **Resource Versioning**: CPU and GPU resources use version numbers to track changes
4. **Bind Group Organization**: 
   - Group 0: Frame uniforms (camera, time)
   - Group 1: Light data
   - Group 2: Object uniforms (model matrix)
   - Group 3: Material data (properties, textures)

## Important Tips

1. Always check WebGPU resource creation success with validation
2. Understand the difference between CPU resources (`Texture`, `Material`) and GPU resources (`WebGPUTexture`, `WebGPUMaterial`)
3. When modifying shaders, ensure bind group layouts match the shader definitions
4. For material properties, use the `MaterialProperties` struct for consistency
5. WebGPU resources must be explicitly released to prevent memory leaks

## Key Files for Reference

- `include/engine/rendering/webgpu/WebGPUContext.h`: Core WebGPU context that manages all GPU resources
- `include/engine/rendering/webgpu/WebGPUBindGroupFactory.h`: Factory for creating bind groups and layouts
- `include/engine/rendering/Material.h`: CPU-side material definition
- `resources/shader.wgsl`: Main shader with material and lighting implementation
- `src/engine/Application.cpp`: Main application initialization and render loop
