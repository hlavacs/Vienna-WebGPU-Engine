struct VertexInput {
    @location(0) position: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) depth: f32, // pass depth to fragment
};

struct ShadowPass2DUniforms {
    lightViewProjectionMatrix: mat4x4f,
    lightPos: vec3f,
    farPlane: f32,
};

#include "engine://core/object_uniforms.wgsl"

@group(4) @binding(0)
var<uniform> uShadow: ShadowPass2DUniforms;

@vertex
fn vs_shadow(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = (u_object.modelMatrix * vec4f(in.position, 1.0)).xyz;

    out.position = uShadow.lightViewProjectionMatrix * vec4f(worldPos, 1.0);

    // Linear depth for debug
    let lightToFrag = worldPos - uShadow.lightPos; // uShadow.lightPos must be uniform
    out.depth = length(lightToFrag) / uShadow.farPlane;

    return out;
}

@fragment
fn fs_shadow(in: VertexOutput) -> @location(0) vec4f {
    // Write unbiased depth as grayscale
    let d = clamp(in.depth, 0.0, 1.0);
    return vec4f(d, d, d, 1.0);
}
