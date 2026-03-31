# Bind Group System

## Overview

The bind group system provides automatic bind group management with name-based resolution and smart state tracking. All bind groups are resolved by shader-defined names, not fixed indices.

**Key Features:**
- **Name-based resolution**: Bind groups found by name, not index
- **Automatic state tracking**: Only rebinds when state changes (camera, object, material)
- **Reuse policies**: Global, PerFrame, PerObject, PerMaterial
- **Unified handling**: Custom and built-in bind groups use the same logic

## Architecture

### Components

- **BindGroupBinder**: Centralized binding with automatic state tracking
- **FrameCache**: Frame-wide bind group cache (frame, object, custom)
- **WebGPUShaderInfo**: Stores bind group name → index mappings
- **BindGroupDataProvider**: Custom uniform data provider for scene nodes

### Flow

```
1. Scene collects BindGroupDataProviders from nodes
2. FrameCache processes providers → creates/caches bind groups
3. RenderPass calls binder.bind(renderPass, pipeline, cameraId, bindGroups, objectId, materialId)
4. Binder:
   - Iterates shader's bind groups (by name)
   - Resolves index via shaderInfo->getBindGroupIndex(name)
   - Fetches from cache/parameter based on type and reuse policy
   - Only binds if state changed or not yet bound
5. GPU rendering
```

## Bind Group Index Resolution

### How It Works

1. **Shader defines bind groups** with `@group(N)` annotations
2. **ShaderFactory parses** bind group names and types from shader source
3. **WebGPUShaderInfo stores** name-to-index mapping (`m_nameToIndex`)
4. **BindGroupBinder queries** index via `shaderInfo->getBindGroupIndex(layoutInfo->getName())`
5. **setBindGroup()** called with resolved index

**Example Flow:**
```cpp
// Shader: @group(2) @binding(0) var<uniform> u_object: ObjectUniforms;
// ShaderInfo maps: "ObjectUniforms" -> 2

auto indexOpt = shaderInfo->getBindGroupIndex("ObjectUniforms");
// Returns: 2
renderPass.setBindGroup(2, objectBindGroup, 0, nullptr);
```

### Index Requirements

- **Group 0**: Reserved for `FrameUniforms` (if used by shader)
- **All other groups**: Defined by the shader `@group(N)` annotations
- **Custom bind groups**: Can use any available index (typically 5+)

## Bind Group Types

### Built-in Bind Groups

| Type | Reuse Policy | Source | Description |
|------|--------------|--------|-------------|
| Frame | PerFrame | Cache (cameraId) | Camera view/projection |
| Light | PerFrame | Parameter | Scene lights |
| Object | PerObject | Parameter or cache (objectId) | Model matrices |
| Material | PerObject | Parameter | Material properties/textures |
| Shadow | PerFrame | Parameter | Shadow maps |
| Debug | Global | Parameter | Debug primitives |

### Custom Bind Groups

User-defined via `BindGroupDataProvider`:

- **Global**: Never changes (app settings)
- **PerFrame**: Changes per camera (time, frame data)
- **PerObject**: Unique per object (animation state)
- **PerMaterial**: Unique per material (variants)

**All bind groups** (built-in and custom) use the same unified logic based on reuse policy.

## Usage

### Creating Custom Bind Groups

```cpp
// In your custom RenderNode class
class MyCustomNode : public UpdateNode
{
public:
    void preRender(std::vector<BindGroupDataProvider>& outProviders) override
    {
        // Define your uniform struct
        struct MyUniforms {
            float time;
            glm::vec3 color;
            float intensity;
        };

        MyUniforms uniforms{
            currentTime,
            myColor,
            intensity
        };

        // Shared bind group (Global/PerFrame)
        outProviders.push_back(BindGroupDataProvider::create(
            "MyShader",              // Shader name
            "MyCustomData",          // Bind group name in shader
            uniforms,                // Uniform data
            BindGroupReuse::PerFrame // Reuse policy
        ));

        // Per-object bind group (PerObject/PerMaterial)
        uint64_t objectId = reinterpret_cast<uint64_t>(this);
        outProviders.push_back(BindGroupDataProvider::create(
            "MyShader",
            "PerObjectData",
            uniforms,
            BindGroupReuse::PerObject,
            objectId                 // Instance ID for per-object caching
        ));
    }
};
```

### Shader Definition

```wgsl
// Frame uniforms MUST be at group 0 if used
@group(0) @binding(0) var<uniform> u_frame: FrameUniforms;

// Other engine bind groups (indices resolved by name)
@group(1) @binding(0) var<storage, read> u_lights: LightsBuffer;
@group(2) @binding(0) var<uniform> u_object: ObjectUniforms;

// Custom bind groups can use any available index (typically 5+)
// Index doesn't matter - resolved by name "MyCustomData"
@group(5) @binding(0) var<uniform> myCustomData: MyUniforms;

// Another custom bind group - index doesn't matter
// Resolved by name "PerObjectData"
@group(6) @binding(0) var<uniform> perObjectData: MyUniforms;

struct MyUniforms {
    time: f32,
    color: vec3<f32>,
    intensity: f32,
}
```

**Key Points:**
- Group indices in shader can be **any value** (except 0 is reserved for Frame)
- BindGroupBinder uses **name** to find the correct index: `shaderInfo->getBindGroupIndex("MyCustomData")`
- You could use `@group(10)` or `@group(99)` - the system resolves it dynamically
- Bind group names must be unique within a shader

### Shader Registration

```cpp
// In ShaderRegistry or shader setup code
auto shader = shaderBuilder
    .setShaderPath("path/to/shader.wgsl")
    .addCustomBindGroup(
        "MyCustomData",           // Bind group name (must match shader)
        BindGroupReuse::PerFrame, // Reuse policy
        {
            // Define bindings
            {0, "myUniforms", BindingType::UniformBuffer, sizeof(MyUniforms)}
        }
    )
    .addCustomBindGroup(
        "PerObjectData",           // Bind group name (must match shader)
        BindGroupReuse::PerObject,
        {
            {0, "perObjectUniforms", BindingType::UniformBuffer, sizeof(MyUniforms)}
        }
    )
    .build();

// The system will:
// 1. Parse shader to find @group(N) indices
// 2. Store mapping: "MyCustomData" -> 5, "PerObjectData" -> 6
// 3. Use these mappings during rendering
```

## Automatic State Tracking

### How It Works

```cpp
// First object - all groups bound
binder.bind(renderPass, pipeline, cameraId, bindGroups, objectId1, materialId1);

// Second object - only Object group rebound (same camera, different object)
binder.bind(renderPass, pipeline, cameraId, bindGroups, objectId2, materialId1);

// Third object - Object + Material rebound
binder.bind(renderPass, pipeline, cameraId, bindGroups, objectId2, materialId2);

// New render pass - all groups rebound automatically 
auto newPass = encoder.beginRenderPass(&desc);
binder.bind(newPass, shaderInfo, cameraId2, bindGroups, objectId2);  // All rebound
```

**How Tracking Works:**
```cpp
// BindGroupBinder::bindGroupAtIndex()
auto it = m_boundBindGroups.find(groupIndex);
if (it != m_boundBindGroups.end() && it->second == bindGroup.get()) {
    // Already bound at this index, skip
    return true;
}
renderPass.setBindGroup(groupIndex, bindGroup->getBindGroup(), 0, nullptr);
m_boundBindGroups[groupIndex] = bindGroup.get();  // Track
```

### Reuse Policy Selection

Choose the appropriate reuse policy based on your data:

| Use Case | Policy | Instance ID |
|----------|--------|-------------|
| Global settings, time | Global | None |
| Per-frame data (same for all objects) | PerFrame | None |
| Animation state, instance data | PerObject | Object ID |
| Material variant properties | PerMaterial | Material ID |

## Best Practices

### 1. Minimize Per-Object Bind Groups

Per-object bind groups create separate GPU resources for each instance. Prefer:
- **Good**: One PerFrame bind group shared by 1000 objects
- **Avoid**: 1000 PerObject bind groups with identical data

### 2. Group Related Data

Combine related uniforms into a single bind group:
```cpp
// Good - single bind group
struct AnimationData {
    float animationTime;
    glm::mat4 boneMatrices[64];
    float blendWeights[4];
};

// Avoid - multiple small bind groups
// (overhead of multiple bind group bindings)
```

### 3. Update Only When Changed

`processBindGroupProviders()` only uploads data when it differs:
```cpp
// In preRender() - only provide data if it changed
if (animationStateChanged) {
    outProviders.push_back(BindGroupDataProvider::create(...));
}
```

### 4. Understand Name-Based Resolution

The system uses **bind group names**, not fixed indices:
```cpp
// WRONG - Assuming fixed indices
renderPass.setBindGroup(0, frameBindGroup, ...);   // Frame
renderPass.setBindGroup(1, lightBindGroup, ...);   // Light
renderPass.setBindGroup(2, objectBindGroup, ...);  // Object

// CORRECT - Name-based resolution via BindGroupBinder
binder.bind(renderPass, shaderInfo, cameraId, {
    {BindGroupType::Light, lightBindGroup},
    {BindGroupType::Object, objectBindGroup}
});
// System resolves: "LightBuffer" -> index 1, "ObjectUniforms" -> index 2
```

### 5. Understand State Tracking

The binder automatically optimizes bind group bindings:
```cpp
// Automatic detection of changes
binder.bind(renderPass, shaderInfo, cameraId1, bindGroups, objectId1);
binder.bind(renderPass, shaderInfo, cameraId1, bindGroups, objectId2);  // Only Object rebound
binder.bind(renderPass, shaderInfo, cameraId2, bindGroups, objectId2);  // Only Frame rebound

// New render pass automatically detected
auto newPass = encoder.beginRenderPass(&desc);
binder.bind(newPass, shaderInfo, cameraId2, bindGroups, objectId2);  // All rebound
```
## Troubleshooting

- **Bind group not found**: Shader name and bind group name must match exactly (case-sensitive).
- **Data not updating**: Ensure `preRender()` supplies data each frame and the struct size matches the shader.
- **Wrong bind group bound**: Check instance ID for PerObject/PerMaterial reuse policies.
- **Index resolution fails**: Verify `shaderInfo->getBindGroupIndex(name)` returns a value.

## Advanced Topics

### Bind Group Index Assignment

The system automatically assigns indices during shader parsing:

```cpp
// WebGPUShaderFactory parses shader
// Finds: @group(5) @binding(0) var<uniform> myData: MyUniforms;

// Creates WebGPUBindGroupLayoutInfo
auto layoutInfo = std::make_shared<WebGPUBindGroupLayoutInfo>(
    "MyCustomData",              // Name for lookup
    BindGroupType::Custom,       // Type
    BindGroupReuse::PerFrame,    // Reuse policy
    bindings                     // Binding definitions
);

// Adds to shader info with actual shader index
shaderInfo->addBindGroupLayout(5, layoutInfo);
// This stores: m_nameToIndex["MyCustomData"] = 5
```

### Multi-Shader Compatibility

Different shaders can use different indices for the same logical bind group:

```wgsl
// ShaderA.wgsl
@group(0) @binding(0) var<uniform> u_frame: FrameUniforms;
@group(3) @binding(0) var<uniform> u_object: ObjectUniforms;

// ShaderB.wgsl
@group(0) @binding(0) var<uniform> u_frame: FrameUniforms;
@group(7) @binding(0) var<uniform> u_object: ObjectUniforms;
```

Both work correctly! The system resolves by **name**:
- ShaderA: "ObjectUniforms" -> 3
- ShaderB: "ObjectUniforms" -> 7

### Name Resolution Flow

```
1. RenderPass calls: binder.bind(renderPass, shaderInfo, ...)
2. Binder iterates: shaderInfo->getBindGroupLayoutVector()
3. For each layout:
   - Resolve index via shaderInfo->getBindGroupIndex(layoutName)
   - Fetch bind group based on type and reuse policy
   - Bind only if changed
```

## Future Extensions

- Automatic uniform reflection
- Bind group validation for layout sizes
- Hot reload on shader changes
- Usage metrics and bind group statistics
