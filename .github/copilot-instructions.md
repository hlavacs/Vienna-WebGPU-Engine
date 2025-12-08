# Vienna-WebGPU-Engine Codebase Guide


This document provides essential knowledge to help AI coding assistants be productive when working with the Vienna-WebGPU-Engine codebase.

## Build System and Troubleshooting

### Understanding Build Output

**CRITICAL:** When using VS Code tasks or commands that run `build.bat`, the task/command system may report success even when the build actually failed! You MUST:

1. **Always check the actual terminal output** using `run_in_terminal` or `get_terminal_output` to see the real build result
2. Look for `[SUCCESS] Build completed successfully!` at the end of the output - this is the true success indicator
3. If you see `[ERROR] Build failed.` in the output, the build failed regardless of what the task system reports
4. MSVC compiler errors appear in the format: `filename(line,column): error C####: description`
5. The build script (`scripts/build.bat`) launches a Visual Studio Developer Command Prompt and runs cmake/ninja

**Common Error Patterns:**
- `error C2039: "X" ist kein Member von "Y"` - Method/member doesn't exist in class Y
- `error C2665: "X": Keine überladene Funktion` - No matching overload for function X
- Look for `FAILED:` lines in the build output to see which file failed to compile
- Read error messages carefully - they include file path, line number, and the actual issue

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

## Coding Style & Conventions

**Indentation:** Use tabs for indentation, not spaces. All C++ and shader files should consistently use tab-based formatting.

**Naming Conventions:** Use `camelCase`

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

### Resource Loaders

**Location**: `include/engine/resources/loaders/`

All loaders derive from base classes and follow a consistent loading pattern:

1. **GeometryLoader** (`GeometryLoader.h`) - Abstract base class
   - Base for ObjLoader and GltfLoader
   - Handles coordinate system transformations
   - Method: `loadMesh(path, outMesh, ...)` - Load geometry into Mesh object

2. **ObjLoader** (`ObjLoader.h`)
   - Loads Wavefront .obj files
   - Supports materials via .mtl files
   - Default coordinate system: +Y up, -Z forward (OpenGL convention)

3. **GltfLoader** (`GltfLoader.h`)
   - Loads glTF/GLB files (binary and text)
   - Supports embedded materials and textures
   - Default coordinate system: +Y up, +Z forward (glTF convention)

4. **TextureLoader** (`TextureLoader.h`)
   - Loads image files (PNG, JPG, etc.) using stb_image
   - Method: `loadTexture(path, outTexture)` - Load texture into Texture object
   - Handles sRGB and linear color spaces

**Loading Pattern:**
```cpp
// 1. Create managers with loaders
auto textureLoader = std::make_shared<TextureLoader>(basePath);
auto textureManager = std::make_shared<TextureManager>(textureLoader);

// 2. Load resource (CPU-side)
auto textureHandle = textureManager->loadTexture("path/to/texture.png");

// 3. Create GPU resource via factory
auto gpuTexture = context.textureFactory().createFromHandle(textureHandle);
```

## Common Patterns and Conventions

1. **Resource Cleanup**: WebGPU resources need explicit cleanup with `.release()`
2. **Factory Pattern**: All GPU resources are created through factory classes
3. **Resource Versioning**: CPU and GPU resources use version numbers to track changes
4. **Bind Group Organization**: 
   - Group 0: Frame uniforms (camera, time)
   - Group 1: Light data
   - Group 2: Object uniforms (model matrix)
   - Group 3: Material data (properties, textures)
5. **Eager vs Lazy Resource Creation**:
   - **Eager (ShaderFactory)**: Buffers with known sizes (uniforms, storage buffers) → Created immediately during shader build
   - **Lazy (WebGPUMaterial)**: Resources depending on material data (textures, samplers) → Created when all dependencies are ready
   - **Reusable Buffers**: Material property buffers are created once and updated via `writeBuffer()`
   - **Recreated Bind Groups**: Bind groups are recreated when texture references change, but underlying textures persist

## Factory System Reference

**CRITICAL RULE:** Always prefer using existing factories over creating WebGPU resources manually. All GPU resource creation should go through the factory pattern.

### WebGPU Factories (All accessible via WebGPUContext)

The `WebGPUContext` is your gateway to all factories. Access them via:
- `context.meshFactory()` → Creates GPU meshes from CPU mesh data
- `context.textureFactory()` → Creates GPU textures from CPU textures or colors
- `context.materialFactory()` → Creates GPU materials with bind groups
- `context.modelFactory()` → Creates GPU models from CPU model data
- `context.pipelineFactory()` → Creates render pipelines with shader configurations
- `context.samplerFactory()` → Creates texture samplers
- `context.bufferFactory()` → Creates GPU buffers (vertex, index, uniform, storage)
- `context.bindGroupFactory()` → Creates bind groups and layouts for shader resources
- `context.depthTextureFactory()` → Creates depth textures for rendering
- `context.depthStencilStateFactory()` → Creates depth-stencil state configurations
- `context.renderPassFactory()` → Creates render pass configurations
- `context.shaderFactory()` → Creates shaders with automatic bind group layout generation
- `context.shaderRegistry()` → Manages shader instances and hot-reloading

#### BaseWebGPUFactory<SourceT, ProductT>

Most factories inherit from this template base class:
- **Pattern**: `createFrom(source)` or `createFromHandle(handle)` → creates GPU resource from CPU resource
- **Type Safety**: Ensures `SourceT` derives from `Identifiable<SourceT>` for handle-based access
- **Location**: `include/engine/rendering/webgpu/BaseWebGPUFactory.h`

#### Individual Factory Details

1. **WebGPUTextureFactory** (`include/engine/rendering/webgpu/WebGPUTextureFactory.h`)
   - `createFromHandle(handle)` - Create from Texture handle
   - `createFromColor(color, width, height)` - Create solid color texture
   - `createFromDescriptors(textureDesc, viewDesc)` - Create from raw descriptors

2. **WebGPUMaterialFactory** (`include/engine/rendering/webgpu/WebGPUMaterialFactory.h`)
   - `createFromHandle(handle)` - Create GPU material with bind groups from CPU Material
   - Automatically creates material property buffer and texture bind groups

3. **WebGPUMeshFactory** (`include/engine/rendering/webgpu/WebGPUMeshFactory.h`)
   - `createFromHandle(handle)` - Create GPU mesh buffers from CPU Mesh data
   - Creates vertex and index buffers automatically

4. **WebGPUModelFactory** (`include/engine/rendering/webgpu/WebGPUModelFactory.h`)
   - `createFromHandle(handle)` - Create GPU model from CPU Model
   - Handles mesh and material conversion automatically

5. **WebGPUBufferFactory** (`include/engine/rendering/webgpu/WebGPUBufferFactory.h`)
   - `createBuffer(desc)` - Create buffer from descriptor
   - `createBufferWithData(data, size, usage)` - Create and upload data
   - `createBufferFromLayoutEntry(layoutInfo, binding, ...)` - Create buffer matching bind group layout
   - Template method for std::vector: `createBufferWithData(vec, usage)`

6. **WebGPUBindGroupFactory** (`include/engine/rendering/webgpu/WebGPUBindGroupFactory.h`)
   - `createUniformBindGroupLayoutEntry<T>(binding, visibility)` - Helper for uniform buffers
   - `createStorageBindGroupLayoutEntry(...)` - Helper for storage buffers
   - `createTextureBindGroupLayoutEntry(...)` - Helper for textures
   - `createSamplerBindGroupLayoutEntry(...)` - Helper for samplers
   - `createBindGroupLayout(layoutInfo)` - Create bind group layout
   - `createBindGroup(...)` - Create bind group with resources

7. **WebGPUPipelineFactory** (`include/engine/rendering/webgpu/WebGPUPipelineFactory.h`)
   - `createPipeline(descriptor, layouts, layoutCount)` - Create render pipeline
   - `createRenderPipelineDescriptor(...)` - Helper to build pipeline descriptors
   - `getDefaultRenderPipeline()` - Returns cached default pipeline
   - `createPipelineLayout(layouts, count)` - Create pipeline layout

8. **ShaderFactory** (`include/engine/rendering/webgpu/ShaderFactory.h`)
   - `createShader(path, entryPoint, stage)` - Load and create shader module
   - `createShaderWithBindGroups(...)` - Create shader with automatic bind group layout generation
   - Supports uniform buffers, storage buffers, textures, and samplers
   - Returns `WebGPUShaderInfo` with shader module and bind group layouts

9. **WebGPUDepthTextureFactory** (`include/engine/rendering/webgpu/WebGPUDepthTextureFactory.h`)
   - `createDefault(width, height, format)` - Create default depth texture
   - `create(width, height, format, mipLevels, arrayLayers, sampleCount, usage)` - Fully configurable

10. **WebGPUSamplerFactory** (`include/engine/rendering/webgpu/WebGPUSamplerFactory.h`)
    - `createDefaultSampler()` - Create default texture sampler

11. **WebGPURenderPassFactory** (`include/engine/rendering/webgpu/WebGPURenderPassFactory.h`)
    - Creates render pass configurations with color and depth attachments

12. **WebGPUDepthStencilStateFactory** (`include/engine/rendering/webgpu/WebGPUDepthStencilStateFactory.h`)
    - Creates depth-stencil state configurations for pipelines

### Resource Managers (CPU-Side)

All managers derive from `ResourceManagerBase<T>` which provides:
- Resource registration, lookup by ID/handle
- Automatic handle resolution via `Handle<T>::get()`
- Resource lifecycle management

**Location**: `include/engine/resources/`

1. **TextureManager** (`TextureManager.h`)
   - Manages CPU-side `Texture` objects
   - Loads textures via `TextureLoader`
   - Method: `loadTexture(path)` → returns `Texture::Handle`

2. **MeshManager** (`MeshManager.h`)
   - Manages CPU-side `Mesh` objects
   - Stores vertex/index data, material references

3. **MaterialManager** (`MaterialManager.h`)
   - Manages CPU-side `Material` objects
   - Stores material properties and texture references
   - Requires `TextureManager` for texture loading

4. **ModelManager** (`ModelManager.h`)
   - Manages CPU-side `Model` objects (collections of meshes)
   - Requires `MeshManager` and `MaterialManager`
   - Loads models via `ObjLoader` or `GltfLoader`

5. **ResourceManager** (`ResourceManager.h`)
   - Aggregate manager containing all sub-managers
   - Provides high-level loading methods
   - Members: `m_textureManager`, `m_meshManager`, `m_materialManager`, `m_modelManager`
   - Loaders: `m_objLoader`, `m_gltfLoader`, `m_textureLoader`

### Rendering Managers

1. **PipelineManager** (`include/engine/rendering/PipelineManager.h`)
   - Manages render pipelines with hot-reloading support
   - `createPipeline(name, config)` - Create and register pipeline
   - `getPipeline(name)` - Get cached pipeline by name
   - `reloadPipeline(name)` - Reload shader and recreate pipeline
   - `reloadAllPipelines()` - Reload all registered pipelines
   - Stores `PipelineConfig` for each pipeline (shader, formats, layouts, topology, etc.)

2. **RenderPassManager** (`include/engine/rendering/RenderPassManager.h`)
   - Manages render pass configurations
   - `registerPass(passContext)` - Register render pass
   - `beginPass(passId, encoder)` - Begin render pass
   - `updatePassAttachments(passId, colorTexture, depthBuffer)` - Update on resize

3. **RenderBufferManager** (`include/engine/rendering/RenderBufferManager.h`)
   - Double-buffering for render state (thread-safe)
   - `acquireWriteBuffer()` - Get buffer for writing
   - `submitWrite()` - Submit written data
   - `acquireReadBuffer()` - Get buffer for reading

4. **WebGPUSurfaceManager** (`include/engine/rendering/webgpu/WebGPUSurfaceManager.h`)
   - Manages WebGPU surface and swap chain
   - Handles surface creation, resizing, and frame presentation

## Important Tips

1. **ALWAYS use factories** - Never create WebGPU resources (buffers, textures, pipelines, etc.) manually
2. **Access factories through WebGPUContext** - The context owns all factory instances
3. Always check WebGPU resource creation success with validation
4. Understand the difference between CPU resources (`Texture`, `Material`) and GPU resources (`WebGPUTexture`, `WebGPUMaterial`)
5. When modifying shaders, ensure bind group layouts match the shader definitions
6. For material properties, use the `MaterialProperties` struct for consistency
7. WebGPU resources must be explicitly released to prevent memory leaks
8. Use `BaseWebGPUFactory` pattern when creating new factory types
9. **Before creating new GPU resources**, check if a factory already exists for that resource type
10. **Resource flow**: Load via Manager → Create GPU version via Factory → Use in Renderer

## Key Files for Reference

- `include/engine/rendering/webgpu/WebGPUContext.h`: Core WebGPU context that manages all GPU resources, device, and factories.
- `include/engine/rendering/webgpu/WebGPUBindGroupFactory.h`: Factory for creating bind groups and layouts for shader resources.
- `include/engine/rendering/Material.h`: CPU-side material definition, properties, and texture references.
- `resources/shader.wgsl`: Main shader with material and lighting implementation, including bind group layout definitions.
- `src/engine/Application.cpp`: Main application initialization, event loop, and integration point for rendering, input, and UI. Now delegates camera management to the scene.
- `include/engine/Application.h`: Application class definition, including DragState for orbit camera and resource handles.
- `include/engine/scene/Scene.h` / `src/engine/scene/Scene.cpp`: Scene graph management, node hierarchy, and frame lifecycle. Scene always contains a root node and a camera node by default, and manages the active camera.
- `include/engine/scene/CameraNode.h` / `src/engine/scene/CameraNode.cpp`: Camera node implementation, view/projection matrix logic, and camera transform.
- `include/engine/scene/entity/Node.h` / `src/engine/scene/entity/Node.cpp`: Base node class for the scene graph, supporting parent/child relationships and traversal.
- `include/engine/scene/entity/UpdateNode.h` / `src/engine/scene/entity/RenderNode.h`: Specialized node types for update and render logic in the scene graph.
- `src/engine/scene/Transform.cpp`: Transform component for position, rotation, and scale, with Unity-like lazy matrix updates and lookAt logic.

### Node System and Scene Architecture

- The engine uses a hierarchical node system (`Node`), supporting parent/child relationships and traversal. All scene objects are nodes, and the root node is always present.
- The `Scene` class manages the node hierarchy, frame lifecycle (update, lateUpdate, preRender, render, postRender), and the active camera.
- By default, every `Scene` creates a root node and a camera node as a child, and sets the camera as the active camera.
- Camera management is now fully handled by the scene; the application queries the active camera from the scene for all rendering and control logic.
- Specialized node types (`UpdateNode`, `RenderNode`) allow for custom update and render logic to be attached to nodes in the graph.
- The transform system stores local position, rotation, and scale, and updates world matrices lazily, similar to Unity. The `lookAt` method only updates local rotation and marks the transform as dirty.

### EngineContext and System Access

- **EngineContext** (`include/engine/EngineContext.h`): Provides nodes with access to core engine systems without circular dependencies
  - Accessible via `node->engine()` from any node (read-only, const)
  - Provides convenient shortcuts:
    - `engine()->input()` - Input state and events (InputManager)
    - `engine()->gpu()` - GPU device and resources (WebGPUContext)
    - `engine()->resources()` - Asset loading and management (ResourceManager)
    - `engine()->scenes()` - Scene creation and switching (SceneManager)
  - Automatically propagated to child nodes when added to the scene graph
  - Set by `GameEngine` during initialization and passed through `SceneManager` → `Scene` → `Node` hierarchy
  - **Friend access only**: Only `Scene` and `SceneManager` can set the context (protected `setEngineContext()`)

- **GameEngine** is now minimal and focused on core loop responsibilities:
  - Window and WebGPU initialization
  - Main game loop (update, render, physics)
  - Event processing (SDL events, input, window resize)
  - Manages core subsystems: `InputManager`, `PhysicsEngine`, `Renderer`, `ImGuiManager`
  - Does NOT handle scene setup, lighting, models - that's in `main.cpp` or scene-specific code
  
- **InputManager** (`include/engine/input/InputManager.h`):
  - Processes SDL input events via `processEvent(const SDL_Event&)`
  - Query input state: `isKeyPressed(SDL_Scancode)`
  - Accessible from nodes via `engine()->input()`
  
**Example - Node accessing engine systems:**
```cpp
// In your custom UpdateNode::update()
if (engine()->input()->isKeyPressed(SDL_SCANCODE_W)) {
    // Move forward
    getTransform()->translate(glm::vec3(0, 0, speed * deltaTime));
}

// Load a texture
auto textureHandle = engine()->resources()->m_textureManager->loadTexture("path/to/texture.png");

// Access GPU context
auto device = engine()->gpu()->getDevice();
```

