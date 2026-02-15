# Custom Bind Group System

## Overview

The custom bind group system provides a flexible, declarative way for scene nodes to provide uniform data to shaders without directly managing WebGPU resources. The system automatically handles bind group creation, caching, binding, and resource management.

**IMPORTANT**: Bind groups are **resolved by name**, not by fixed indices. The only requirement is that `FrameUniforms` (if used) must be at group index 0. All other bind groups can be at any index and are resolved dynamically based on their name in the shader.

## Architecture

### Components

1. **BindGroupDataProvider** - Data provider struct for custom uniforms
2. **BindGroupBinder** - Centralized bind group binding system with name-based resolution
3. **FrameCache** - Frame-wide resource cache and manager
4. **Scene Integration** - Automatic collection and processing pipeline
5. **WebGPUShaderInfo** - Stores name-to-index mappings for bind groups

### Data Flow

```
Scene Node (preRender)
    │
    ├──> Creates BindGroupDataProvider
    │
    ▼
Scene::preRender()
    │
    ├──> Collects providers from all enabled nodes
    │
    ▼
Renderer::renderFrame()
    │
    ├──> FrameCache::processBindGroupProviders()
    │    │
    │    ├──> Creates/updates bind groups
    │    └──> Caches in customBindGroupCache
    │
    ▼
RenderPass (MeshPass, ShadowPass, etc.)
    │
    ├──> BindGroupBinder::bind()
    │    │
    │    ├──> Iterates shader's bind group layouts
    │    ├──> Resolves each by name via shaderInfo->getBindGroupIndex(name)
    │    ├──> Fetches from caches based on type
    │    ├──> Binds to correct group index
    │    └──> Tracks to avoid redundant binds
    │
    ▼
GPU Rendering
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
- **All other groups**: Dynamically assigned based on shader definition order
- **Custom bind groups**: Can use any available index (typically 5+)

## Bind Group Types

### Engine-Defined Bind Groups

| Type | Description | Resolution | Typical Index* |
|------|-------------|------------|---------------|
| Frame | Camera uniforms (view, projection, position) | `frameBindGroupCache[cameraId]` | 0 (required) |
| Light | Light array for scene | Parameter to `bind()` | 1 |
| Object | Model/normal matrices | Parameter or `objectBindGroupCache[objectId]` | 2 |
| Material | Material properties and textures | Parameter to `bind()` | 3 |
| Shadow | Shadow maps and sampler | Parameter to `bind()` | 4 |

*Indices are examples from PBR_Lit_Shader.wgsl - actual indices are shader-specific and resolved by name

### Specialized Pass Bind Groups

| Type | Description | Usage | Index Resolution |
|------|-------------|-------|------------------|
| ShadowPass2D | Uniforms for 2D shadow rendering | Shadow map rendering pass | By name in shadow shader |
| ShadowPassCube | Uniforms for cube shadow rendering | Cube shadow map rendering pass | By name in shadow shader |
| Debug | Debug primitives data | Debug visualization pass | By name in debug shader |
| Mipmap | Mipmap generation parameters | Mipmap generation pass | By name in mipmap shader |

### Custom Bind Groups

User-defined bind groups created via `BindGroupDataProvider` with configurable reuse policies:

- **Global/PerFrame**: Shared across all objects (e.g., global time, settings)
- **PerObject/PerMaterial**: Unique per instance (e.g., animation state, per-object data)

Custom bind groups are resolved by name and can use any available index in the shader.

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

## Caching Strategy

### Cache Keys

Custom bind groups use string keys for caching:

- **Shared (Global/PerFrame)**: `"ShaderName:BindGroupName"`
- **Per-Instance (PerObject/PerMaterial)**: `"ShaderName:BindGroupName:InstanceId"`

Example:
```cpp
// Shared cache key
"MyShader:MyCustomData"


// First call - binds all groups
binder.bind(renderPass, shaderInfo, cameraId, bindGroups, objectId);

// Subsequent calls - only binds changed groups
for (const auto& item : items) {
    binder.bind(renderPass, shaderInfo, cameraId, 
                {{BindGroupType::Object, item.objectBindGroup}}, 
                item.objectId);
    // Only object bind group rebound if different
    // Frame, Light, etc. remain bound from previous call
}

// Reset tracking when pipeline changes
binder.reset();
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
### Bind Group Binding Optimization

`BindGroupBinder` tracks currently bound bind groups to avoid redundant `setBindGroup()` calls:

```cpp
BindGroupBinder binder(&frameCache);
binder.setCameraId(cameraId);

// First call - binds all groups
binder.bindShaderGroups(renderPass, shaderInfo, ...);

// Subsequent calls - only binds changed groups
for (const auto& item : items) {
    binder.setObjectId(item.id);
    binder.bindShaderGroups(renderPass, shaderInfo, item.objectBindGroup, ...);
    // Only object bind group rebound if different
}

// Reset tracking when pipeline changes
binder.reset();
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
if 

### 5. Understand Name-Based Resolution

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
```(animationStateChanged) {
    outProviders.push_back(BindGroupDataProvider::create(...));
}
```

### 4. Call reset() on Pipeline Changes

When changing render pipelines, call `binder.reset()` to clear tracking state:
```cpp
if (newPipeline != currentPipeline) {
    renderPass.setPipeline(newPipeline->getPipeline());
    binder.reset();  // Force rebind of all groups
}
```
 **exactly** (case-sensitive)
3. Ensure `preRender()` is being called and providing data
4. Verify shader has the bind group layout defined with correct name
5. Check `shaderInfo->getBindGroupIndex("BindGroupName")` returns a value

### Wrong Bind Group Bound

**Error**: WebGPU validation error about incompatible bind groups

**Solutions**:
1. Check instance ID matches reuse policy (PerObject should have ID)
2. Verify bind group name in code matches shader `@group(N)` annotation
3. Ensure `reset()` called when pipeline changes
4. Verify bind group layout matches shader struct definition

### Index Resolution Failures

**Problem**: Bind group not binding even though it exists

**Solutions**:
1. Check `shaderInfo->getBindGroupIndex(name)` returns correct index
2. Verify shader parsing found the bind group during shader creation
3. Ensure bind group name is unique in the shader
4. Check shader compilation succeeded with correct group indic

### Render Pass Integration

All render passes use `BindGroupBinder` for consistent binding:

- **MeshPass**: Main geometry rendering
- **ShadowPass**: Shadow map rendering
- **DebugPass**: Debug visualization
- **CompositePass**: Post-processing (manual binding for dynamic textures)

## Troubleshooting

### Bind Group Not Found

**Error**: `Custom bind group 'X' not found for shader 'Y'`

**Solutions**:
1. Verify shader name matches exactly
2. Check bind group name matches shader definition
3. Ensure `preRender()` is being called
4. Verify shader has the bind group layout defined

### Wrong Bind Group Bound

**Error**: WebGPU validation error about incompatible bind groups

**Solutions**:
1. Check instance ID matches reuse policy (PerObject should have ID)
2. Verify `setCameraId()` or `setObjectId()` called before binding
3. Ensure `reset()` called when pipeline changes

### Data Not Updating

**Problem**: Uniform data doesn't reflect in shader

**Solutions**:
1. Verify data is being provided every frame via `preRender()`
2. Check data size matches shader struct
3. Ensure cache key is correct for reuse policy

## Example: Time-Based Animation

Complete example of a node that animates based on global time:

```cpp
class PulsingNode : public UpdateNode
{
public:
    void update(float deltaTime) override {
   Advanced Topics

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

Complete resolution flow:

```
1. RenderPass calls: binder.bind(renderPass, shaderInfo, ...)
2. Binder iterates: shaderInfo->getBindGroupLayoutVector()
3. For each layout:
   a. Get name: layoutInfo->getName()
   b. Resolve index: shaderInfo->getBindGroupIndex(name) -> N
   c. Fetch bind group based on type (Frame, Light, Custom, etc.)
   d. Check tracking: m_boundBindGroups[N] != bindGroup?
   e. If different: renderPass.setBindGroup(N, bindGroup, ...)
   f. Update tracking: m_boundBindGroups[N] = bindGroup
```

## Future Extensions

Potential enhancements to the system:

1. **Automatic Uniform Binding**: Reflect shader bindings at runtime
2. **Bind Group Validation**: Verify data size matches shader layout
3. **Hot Reload Support**: Update bind groups when shaders reload (partially implemented)
4. **Statistics**: Track bind group usage and performance metrics
5. **Buffer Pooling**: Reuse GPU buffers across frames for identical data
6. **Index Auto-Assignment**: Automatically assign optimal indices based on usage patterns

        TimeData data{
            m_time,
            2.0f,  // Pulsations per second
            0.5f   // Amplitude
        };

        outProviders.push_back(BindGroupDataProvider::create(
            "PulseShader",
            "TimeData",
            data,
            BindGroupReuse::PerFrame  // Shared across all pulsing objects
        ));
    }

private:
    float m_time = 0.0f;
};
```

## Future Extensions

Potential enhancements to the system:

1. **Automatic Uniform Binding**: Reflect shader bindings at runtime
2. **Bind Group Validation**: Verify data size matches shader layout
3. **Hot Reload Support**: Update bind groups when shaders reload
4. **Statistics**: Track bind group usage and performance metrics
5. **Buffer Pooling**: Reuse GPU buffers across frames for identical data
