// Tutorial 02 - Complete this shader step by step
// Follow the guide in doc/tutorials/02_custom_bindgroup.md
//
// The engine's Frame (@group(0)) and Object (@group(3)) uniform structs are
// auto-generated from C++ via the codegen pipeline. Pulling them in by URI
// keeps this tutorial in lockstep with whatever the engine actually binds —
// no risk of the inline struct drifting from the C++ side. Custom bind groups
// belong on @group(4..7); slots 0..3 are reserved for engine roles.
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

// Tutorial 02 - Step 3: Add TileUniforms struct



@group(2) @binding(0)
var<uniform> unlitMaterialUniforms: UnlitMaterialUniforms;
@group(2) @binding(1)
var textureSampler: sampler;
@group(2) @binding(2)
var baseColorTexture: texture_2d<f32>;

// Tutorial 02 - Step 4: Declare custom bind group (use @group(4) or higher)

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
    // Tutorial 02 - Step 5: Modify fragment shader to use TileUniforms
    let textureColor = textureSample(baseColorTexture, textureSampler, input.texCoord);
    let finalColor = textureColor * unlitMaterialUniforms.color;
    return finalColor;
}
