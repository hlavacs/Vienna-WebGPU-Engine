struct VertexInput {
    @location(0) position: vec3f,
}

;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
}

;

struct ShadowCubeUniforms {
    lightPosition: vec3f,
    farPlane: f32,
}

;

@group(0) @binding(0)
var<uniform> uShadowCube: ShadowCubeUniforms;

// The view-projection is implicit: we'll compute vector from light to vertex in fragment
@vertex
fn vs_shadow_cube(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    // Pass world position to fragment
    out.worldPos = in.position;
    // Position output is required but ignored in depth-only rendering
    out.position = vec4f(0.0, 0.0, 0.0, 1.0);
    return out;
}

@fragment
fn fs_shadow_cube(in: VertexOutput) -> @location(0) f32 {
    // Compute vector from light to vertex
    let lightToFrag = in.worldPos - uShadowCube.lightPosition;
    let distance = length(lightToFrag);

    // Map distance to [0,1] range for depth
    return distance / uShadowCube.farPlane;
}
