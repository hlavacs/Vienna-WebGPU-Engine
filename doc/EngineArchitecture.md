# Vienna-WebGPU-Engine Architecture

This document describes the fundamental architecture, design patterns, and coding conventions used in the Vienna-WebGPU-Engine.

## Table of Contents

1. [Core Architecture](#core-architecture)
2. [Layered Design](#layered-design)
3. [Resource Management](#resource-management)
4. [Factory Pattern](#factory-pattern)
5. [Rendering System](#rendering-system)
6. [Scene Graph](#scene-graph)
7. [Coding Conventions](#coding-conventions)
8. [Key Design Principles](#key-design-principles)

---

## Path Management

**Two path systems via `PathProvider`:**

1. **Application Assets**: `getTextures()`, `getModels()` → `<example>/assets/`
2. **Engine Resources**: `getResource()` → `<engine>/resources/`

**Rules:**
- Always use `PathProvider` (never hardcode paths)
- Debug: CMake defines, Release: runtime detection
- See [CorePrinciples.md](CorePrinciples.md#0-path-management) for details

---

## Core Architecture

The engine follows a **layered architecture** with clear separation of concerns:

```
┌─────────────────────────────────────┐
│      Application Layer              │
│  (Game Loop, Input, UI)             │
└─────────────────────────────────────┘
           ↓
┌─────────────────────────────────────┐
│      Scene Graph Layer              │
│  (Nodes, Transforms, Camera)        │
└─────────────────────────────────────┘
           ↓
┌─────────────────────────────────────┐
│      Rendering Layer                │
│  (Renderer, Passes, Pipelines)      │
└─────────────────────────────────────┘
           ↓
┌─────────────────────────────────────┐
│      Resource Layer                 │
│  (Managers, Loaders, Factories)     │
└─────────────────────────────────────┘
           ↓
┌─────────────────────────────────────┐
│      Core Layer                     │
│  (Handle, Identifiable, Versioned)  │
└─────────────────────────────────────┘
```

---

## Layered Design

### 1. Core Layer
**Location:** `include/engine/core/`

Foundation classes used throughout the engine:

- **`Identifiable<T>`**: Provides unique IDs for resources
- **`Versioned`**: Tracks resource versions for dirty checking
- **`Handle<T>`**: Type-safe resource handles with validation

### 2. Resource Layer
**Location:** `include/engine/resources/`

Manages CPU-side resources:

- **Resource Managers**: `TextureManager`, `MeshManager`, `MaterialManager`, `ModelManager`
- **Loaders**: `TextureLoader`, `ObjLoader`, `GltfLoader`
- **CPU Resources**: `Texture`, `Mesh`, `Material`, `Model`

**Key Rules:** 
- All resource loading goes through managers. Never load resources directly.
- Always use `PathProvider` for path resolution
- Use `resolve(key)` for application assets
- Use `getResource(file)` for engine resources

### 3. Rendering Layer
**Location:** `include/engine/rendering/`

Handles GPU-side rendering:

- **WebGPU Factories**: Create GPU resources from CPU resources
- **Render Passes**: `MeshPass`, `ShadowPass`, `DebugPass`
- **Pipeline Management**: `PipelineManager` with hot-reloading
- **Bind Group System**: `BindGroupBinder` for centralized binding

**Key Rule:** All GPU resource creation goes through factories accessed via `WebGPUContext`.

### 4. Scene Graph Layer
**Location:** `include/engine/scene/`

Hierarchical scene representation:

- **`Scene`**: Manages the node hierarchy and frame lifecycle
- **`Node`**: Base class for scene graph nodes
- **`Transform`**: Position, rotation, scale with lazy matrix updates
- **`CameraNode`**: Camera with view/projection matrices

### 5. Application Layer
**Location:** `include/engine/`

Top-level game loop integration:

- **`GameEngine`**: Main loop, event processing, subsystem management
- **`Application`**: User-facing application base class
- **`EngineContext`**: Provides nodes with access to core systems

---

## Resource Management

### CPU Resources → GPU Resources Flow

```
Load via Manager → Create GPU version via Factory → Use in Renderer
```

**Example:**
```cpp
// 1. Load CPU resource via manager
auto textureHandle = textureManager->loadTexture("texture.png");

// 2. Create GPU resource via factory
auto gpuTexture = context.textureFactory().createFromHandle(textureHandle);

// 3. Use in renderer
material->setTexture("albedo", gpuTexture);
```

### Resource Handles

Use `Handle<T>` for type-safe resource references:

```cpp
Texture::Handle textureHandle = textureManager->loadTexture("texture.png");
if (auto texture = textureHandle.get()) {
    // Use texture
}
```

---

## Factory Pattern

**Rule:** Always use factories for GPU resource creation. Never create WebGPU resources manually.

### Accessing Factories

All factories are accessed through `WebGPUContext`:

```cpp
// Texture creation
auto texture = context.textureFactory().createFromHandle(handle);

// Material creation
auto material = context.materialFactory().createFromHandle(handle);

// Mesh creation
auto mesh = context.meshFactory().createFromHandle(handle);

// Pipeline creation
auto pipeline = context.pipelineFactory().createPipeline(descriptor, layouts, count);
```

### Common Factories

| Factory | Purpose | Key Methods |
|---------|---------|-------------|
| `WebGPUTextureFactory` | GPU textures | `createFromHandle()`, `createFromColor()`, `getWhiteTexture()` |
| `WebGPUMaterialFactory` | GPU materials with bind groups | `createFromHandle()` |
| `WebGPUMeshFactory` | GPU mesh buffers | `createFromHandle()` |
| `WebGPUModelFactory` | GPU models | `createFromHandle()` |
| `WebGPUPipelineFactory` | Render pipelines | `createPipeline()`, `getDefaultRenderPipeline()` |
| `WebGPUBufferFactory` | GPU buffers | `createBuffer()`, `createBufferWithData()` |
| `WebGPUBindGroupFactory` | Bind groups and layouts | `createBindGroup()`, `createBindGroupLayout()` |

---

## Rendering System

### Frame Lifecycle

```cpp
1. clearForNewFrame()          // Clear frame cache
2. prepareGPUResources()       // Upload uniforms, create bind groups
3. processBindGroupProviders() // Process custom bind groups
4. Render passes:
   - Shadow pass (optional)
   - Main mesh pass
   - Debug pass (optional)
5. Present frame
```

### Bind Group System

```cpp
binder.bind(renderPass, pipeline, cameraId, bindGroups, objectId, materialId);
```

**Features:**
- Name-based resolution (not fixed indices)
- Automatic state tracking (only rebinds on state change)
- Unified handling via reuse policies
- See [BindGroupSystem.md](BindGroupSystem.md) for details

### Render Passes

| Pass | Purpose | Outputs |
|------|---------|---------|
| `ShadowPass` | Shadow map rendering | 2D and cube shadow maps |
| `MeshPass` | Main scene rendering | Color + depth |
| `DebugPass` | Debug visualization | Debug primitives overlay |

---

## Scene Graph

### Node Hierarchy

Every `Scene` contains:
- **Root node**: Always present
- **Camera node**: Child of root, managed by scene

**Example:**
```cpp
// Scene automatically creates root and camera
auto scene = std::make_shared<Scene>();

// Add custom nodes
auto objectNode = std::make_shared<Node>();
objectNode->getTransform()->setPosition({0, 1, 0});
scene->getRootNode()->addChild(objectNode);
```

### Node Types

- **`Node`**: Base scene graph node
- **`UpdateNode`**: Node with custom update logic
- **`RenderNode`**: Node with custom render logic
- **`CameraNode`**: Camera with view/projection

### Transform System

Unity-like lazy matrix updates:

```cpp
transform->setPosition({0, 1, 0});
transform->setRotation({0, 45, 0});  // Euler angles in degrees
transform->setScale({2, 2, 2});

// Matrices updated lazily on first access
glm::mat4 world = transform->getWorldMatrix();
```

---

## Coding Conventions

### Naming Conventions

- **camelCase** for variables, functions, methods
- **PascalCase** for classes, structs, enums
- **UPPER_CASE** for constants and macros

### Indentation

**Use tabs for indentation**, not spaces.

### Include Order

Use blank lines between groups:

1. Header for implementation file
2. Standard library headers
3. Third-party library headers
4. Project-specific headers
5. Platform-specific headers (optional)

**Example:**
```cpp
#include "MyClass.h"

#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "engine/rendering/Renderer.h"
#include "engine/scene/Scene.h"
```

### Header Guards

Use `#pragma once` for all header files.

### File Structure

Organize by feature/module with clear naming:
```
engine/
├── core/           # Foundation classes
├── resources/      # Resource management
├── rendering/      # Rendering system
│   └── webgpu/     # WebGPU-specific code
├── scene/          # Scene graph
└── input/          # Input handling
```

---

## Key Design Principles

### 1. Factory Pattern for GPU Resources

**Never** create WebGPU resources manually. Always use factories:

```cpp
// ❌ Bad
wgpu::Buffer buffer = device.createBuffer(desc);

// ✅ Good
auto buffer = context.bufferFactory().createBuffer(desc);
```

### 2. Resource Flow: Manager → Factory → Renderer

```cpp
// 1. Manager (CPU)
auto meshHandle = meshManager->load("model.obj");

// 2. Factory (GPU)
auto gpuMesh = context.meshFactory().createFromHandle(meshHandle);

// 3. Renderer (Usage)
gpuMesh->bindBuffers(renderPass, layout);
```

### 3. EngineContext for System Access

Nodes access engine systems via `EngineContext`:

```cpp
// In custom UpdateNode
void update(float deltaTime) override {
    if (engine()->input()->isKeyPressed(SDL_SCANCODE_W)) {
        // Move forward
    }
    
    auto textureHandle = engine()->resources()->m_textureManager->loadTexture("tex.png");
}
```

### 4. Lazy Resource Creation

- **Eager**: Resources with known sizes (uniform buffers)
- **Lazy**: Resources depending on runtime data (textures, materials)

### 5. Explicit Resource Cleanup

WebGPU resources need explicit cleanup:

```cpp
texture.release();
buffer.release();
bindGroup.release();
```

### 6. Bind Group Organization

Standard bind group indices:
- **Group 0**: Frame uniforms (camera)
- **Group 1**: Light data
- **Group 2**: Object uniforms (model matrix)
- **Group 3**: Material data

### 7. Hot-Reloading Support

Use `PipelineManager` for shader hot-reloading:

```cpp
pipelineManager->createPipeline("myShader", config);
// Later...
pipelineManager->reloadPipeline("myShader");
```

---

## Additional Resources

- **[BindGroupSystem.md](BindGroupSystem.md)**: Detailed bind group architecture
- **[CorePrinciples.md](CorePrinciples.md)**: Engine design philosophy
- **[copilot-instructions.md](../.github/copilot-instructions.md)**: Complete development guide

---

## Quick Reference

### Essential Classes

| Class | Purpose | Location |
|-------|---------|----------|
| `WebGPUContext` | Central GPU resource hub | `rendering/webgpu/` |
| `FrameCache` | Frame-wide rendering data | `rendering/` |
| `BindGroupBinder` | Centralized bind group binding | `rendering/` |
| `Scene` | Scene graph root | `scene/` |
| `ResourceManager` | Aggregate resource manager | `resources/` |
| `GameEngipplication assets (correct path usage):**
```cpp
// ✅ Application texture
auto texturePath = PathProvider::resolve("textures") / "myTexture.png";
auto handle = engine()->resources()->m_textureManager->loadTexture(texturePath.string());
auto gpuTexture = engine()->gpu()->textureFactory().createFromHandle(handle);

// ✅ Engine resource
auto engineTexture = PathProvider::getResource("cobblestone_floor_08_diff_2k.jpg");
auto handle2 = engine()->resources()->m_textureManager->loadTexture(engineTextur
**Loading and using a texture:**
```cpp
auto handle = engine()->resources()->m_textureManager->loadTexture("texture.png");
auto gpuTexture = engine()->gpu()->textureFactory().createFromHandle(handle);
```

**Adding a node to the scene:**
```cpp
auto node = std::make_shared<Node>();
scene->getRootNode()->addChild(node);
```

**Binding bind groups:**
```cpp
binder.bind(renderPass, shader, cameraId, {
    {BindGroupType::Material, materialBindGroup}
});
```
