# Rendering Architecture Notes (CPU → GPU → Render)

This document consolidates the current and intended rendering architecture, clarifying
responsibilities, data flow, and lifecycle across **Scene Nodes**, **CPU render data**,
**GPU render objects**, and the **render cycle**.

It reflects:
- Multi-material meshes via submeshes
- WebGPU-specific constraints
- RenderCollector–based rendering
- Dirty / update logic
- Shader & material handling

---

## 0. Path Management

### Two Path Systems

**Application Assets** (`basePath`): Example-specific assets

| Build | Location | Set By |
| Debug | Example source directory | `ASSETS_ROOT_DIR` CMake define | `<example_dir>/assets/` (source) |
| Release | Executable dir | `getExecutablePath()` at runtime | `<exe>/assets/` (deployed) |

**2. Engine Library Resource Paths (`resourceRoot`)**

| Build | `resourceRoot` Value | Determined By | Engine Resources Location |
|-------|---------------------|---------------|---------------------------|
| Debug | Project root + `/resources/` | `DEBUG_ROOT_DIR` + `/resources/` | `<project>/resources/` (source) |
| Release | Library dir + `/resources/` | `getEnginePath()` + `/resources/` | `<lib>/resources/` (deployed) |

**Usage:**

```cpp
// Application assets
auto appTexture = PathProvider::getTextures("brick.png");

// Engine resources
auto engineShader = PathProvider::getResource("PBR_Lit_Shader.wgsl");
```

**Key Rules:**
- Use `PathProvider` for all paths (never hardcode)
- `assets/` = example-specific, `resources/` = engine-wide
- Debug: CMake defines paths, Release: runtime detection

### 0.2 Build Output Locations

```
Project Root/
├── build/
│   └── Windows/
│       ├── Debug/          # Engine library + dependencies
│       └── Release/
├── examples/
│   └── build/
│       └── <example_name>/
│           └── Windows/
│               ├── Debug/  # Example executable + copied assets
│               └── Release/
└── resources/              # Source assets (Debug builds access directly)
```

**Key Points:**
- Debug executables run from project root, accessing `resources/` directly
- Release executables expect assets in their output directory
- CMake automatically copies required assets to Release builds
- Always use `PathProvider` to ensure path portability

---

## 1. Core Design Principles

### 1.1 Single Responsibility

Each layer has a strict responsibility:

| Layer | Responsibility |
|-------|----------------|
| Scene Nodes | Visibility, transforms, scene logic |
| CPU Render Data | Geometry, materials, submesh layout |
| GPU Render Objects | GPU buffers, bind groups, pipelines |
| RenderItem | One draw call |
| Renderer | Sorting, batching, issuing draws |

No layer reaches “up” or sideways.

---

## 2. CPU-Side Data Model

### 2.1 Mesh (CPU)

- Owns vertex and index buffers
- No materials
- No submeshes
- CPU-only geometry
- Versioned

Indented example:

    Mesh
     ├─ vertices[]
     └─ indices[]

### 2.2 Submesh (CPU)

- Defined as ranges into a mesh
- Exists only at the Model level

Indented example:

    struct Submesh
    {
        uint32_t indexOffset;
        uint32_t indexCount;
        MaterialHandle material;
    };

### 2.3 Model (CPU)

- Owns:
  - one Mesh
  - multiple Submeshes
- No GPU logic
- No rendering logic

Indented example:

    Model
     ├─ MeshHandle
     └─ Submesh[]

### 2.4 ObjLoader Output (`ObjGeometryData`)

ObjLoader does **not** create materials or GPU objects.

It outputs:
- Unified indexed geometry
- Material ranges
- Raw `tinyobj::material_t`

Indented example:

    ObjGeometryData
    {
        vertices
        indices
        materials          // tinyobj materials
        materialRanges[]   // offset + count + materialId
    }

This avoids:
- Duplicated meshes
- Material managers in loaders
- GPU knowledge in asset loading

---

## 3. GPU-Side Objects

### 3.1 WebGPUMesh

- Wraps one vertex buffer and one index buffer
- Created once per Mesh
- Shared across all models and submeshes

Indented example:

    WebGPUMesh
     ├─ vertexBuffer
     ├─ indexBuffer
     └─ updateGPUResources()

Submeshes do **not** become separate GPU meshes.

### 3.2 WebGPUMaterial

- Wraps:
  - Bind groups
  - Shader selection
- Knows shader type or custom shader name
- Does not render directly

Indented example:

    WebGPUMaterial
     ├─ shaderType OR customShaderName
     ├─ textures[]
     ├─ bindGroup
     └─ updateGPUResources()

Shader lookup happens via:

    context.shaderRegistry()

### 3.3 ShaderRegistry

- Owns `WebGPUShaderInfo`
- Separates:
  - Default shaders (`ShaderType`)
  - Custom shaders (string key)

Materials store identifiers, not shader objects.

---

## 4. RenderItem

- Represents one draw call
- Contains references to:
  - Mesh
  - Submesh range
  - Material / Shader info
- Can check and update GPU resources on demand
- Returned by `RenderObject::getRenderItems()` or similar

---

## 5. Scene Nodes

### 5.1 RenderNode

- Virtual Node with hooks:
  - `preRender()`
  - `onRenderCollect(RenderCollector&)`
  - `postRender()`
- Does not know about GPU objects directly
- Responsible for submitting CPU objects to RenderCollector

### 5.2 Example ModelNode

- Holds CPU Model handle
- On `onRenderCollect()`:
  - Gives CPU Model to RenderCollector
  - RenderCollector retrieves / caches GPU objects
  - RenderCollector creates `RenderItem`s for each submesh

---

## 6. Render Cycle

1. **PreRender**
    - Setup state, transform matrices
2. **RenderCollect**
    - Nodes push CPU objects
    - GPU objects created / cached lazily
    - `RenderItem`s collected for batching / sorting
3. **Sort & Batch**
    - Sort by:
        - Shader / pipeline
        - Material
        - Depth (optional)
    - Minimize pipeline / bind group switches
4. **Draw**
    - Iterate `RenderItem`s
    - Bind shader / material
    - Draw mesh / submesh range
5. **PostRender**
    - Cleanup (optional)

---

## 7. GPU Resource Update Logic

- Each GPU object tracks `dirty` flag and CPU version
- On `update()`:
    - Compare CPU version or dirty flag
    - Update GPU buffers / bind groups if needed
- Suggested:
    - Model asks Mesh and Submeshes if they are dirty
    - WebGPUMaterial updates only when shader / textures change
    - Lazy updates: only update when item is actually drawn
