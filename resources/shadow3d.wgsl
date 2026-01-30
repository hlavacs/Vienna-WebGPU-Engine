struct VertexInput {
    @location(0) position: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) world_position: vec3f,
};

struct FragmentOutput {
    @location(0) color: vec4f,
    @builtin(frag_depth) depth: f32,
};

// Uniforms
struct ObjectUniforms {
    modelMatrix: mat4x4f,
};

struct ShadowPassCubeUniform {
    lightViewProjectionMatrix: mat4x4f,  // view-projection matrix for this cube face
    lightPos: vec3f,    // position of the point light
    farPlane: f32,      // far plane for normalization
};

@group(0) @binding(0)
var<uniform> uShadowCube: ShadowPassCubeUniform;

@group(1) @binding(0)
var<uniform> uObject: ObjectUniforms;

@vertex
fn vs_shadow_cube(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = (uObject.modelMatrix * vec4f(in.position, 1.0)).xyz;
    out.world_position = worldPos;

    // Clip-space for rasterization
    out.position = uShadowCube.lightViewProjectionMatrix * vec4f(worldPos, 1.0);
    
    return out;
}

@fragment
fn fs_shadow_cube(in: VertexOutput) -> FragmentOutput {
    var out: FragmentOutput;
    
    // Calculate linear depth
    let light_to_frag = in.world_position - uShadowCube.lightPos;
    let distance = length(light_to_frag);
    let linear_depth = distance / uShadowCube.farPlane;
    
    // Write linear depth to depth buffer
    out.depth = clamp(linear_depth, 0.0, 1.0);
    
    // Debug visualization to color buffer
    out.color = vec4f(linear_depth, linear_depth, linear_depth, 1.0);
    
    return out;
}