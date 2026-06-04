// Tutorial 03 - Unlit Shader with Shadow Mapping
// Extends the basic unlit shader to receive shadows from directional lights
//
// What you'll learn:
// - How to sample shadow maps in shaders
// - Shadow coordinate transformation (world space → light space → shadow map UVs)
// - Percentage Closer Filtering (PCF) for soft shadow edges
// - Working with depth comparison samplers
// - Accessing Light bind group data (light matrices, shadow maps)
//
// This shader demonstrates shadow receiving without implementing full lighting.
// The result is an unlit texture that darkens in shadowed areas.
//
// Engine-side struct decls (FrameUniforms @group(0), ObjectUniforms @group(3))
// are auto-generated from C++ — pulled in by URI so the tutorial stays in
// sync with whatever the engine actually binds.
#include "engine://core/frame_uniforms.wgsl"
#include "engine://core/object_uniforms.wgsl"

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) texCoord: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) texCoord: vec2f,
}

struct UnlitMaterialUniforms {
    color: vec4f,
}


@group(2) @binding(0)
var<uniform> unlitMaterialUniforms: UnlitMaterialUniforms;
@group(2) @binding(1)
var textureSampler: sampler;
@group(2) @binding(2)
var baseColorTexture: texture_2d<f32>;

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let worldPos = u_object.modelMatrix * vec4f(input.position, 1.0);
    let viewPos = u_frame.viewMatrix * worldPos;
    output.position = u_frame.projectionMatrix * viewPos;
    output.texCoord = input.texCoord;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureColor = textureSample(baseColorTexture, textureSampler, input.texCoord);
    let finalColor = textureColor * unlitMaterialUniforms.color;
    return finalColor;
}
