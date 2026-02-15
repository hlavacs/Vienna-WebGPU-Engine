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

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) texCoord: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) texCoord: vec2f,
}

struct FrameUniforms {
    viewMatrix: mat4x4f,
    projectionMatrix: mat4x4f,
    viewProjectionMatrix: mat4x4f,
    cameraPosition: vec3f,
    time: f32,
}

struct ObjectUniforms {
    modelMatrix: mat4x4f,
    normalMatrix: mat4x4f,
}

struct UnlitMaterialUniforms {
    color: vec4f,
}


@group(0) @binding(0)
var<uniform> frameUniforms: FrameUniforms;

@group(1) @binding(0)
var<uniform> objectUniforms: ObjectUniforms;

@group(2) @binding(0)
var<uniform> unlitMaterialUniforms: UnlitMaterialUniforms;
@group(2) @binding(1)
var textureSampler: sampler;
@group(2) @binding(2)
var baseColorTexture: texture_2d<f32>;

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let worldPos = objectUniforms.modelMatrix * vec4f(input.position, 1.0);
    let viewPos = frameUniforms.viewMatrix * worldPos;
    output.position = frameUniforms.projectionMatrix * viewPos;
    output.texCoord = input.texCoord;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureColor = textureSample(baseColorTexture, textureSampler, input.texCoord);
    let finalColor = textureColor * unlitMaterialUniforms.color;
    return finalColor;
}
