// Fullscreen quad shader: samples per-camera HDR texture and tone-maps it
// to the (sRGB) surface. Centralising the tonemap here means every per-camera
// pass (deferred composition, skybox, future transparency) can keep writing
// raw linear HDR and we apply the curve in exactly one place.

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) texCoord: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;

    // Single fullscreen triangle covering (-1,-1) .. (3,3).
    let x = f32((vertexIndex & 1u) << 2u) - 1.0;
    let y = f32((vertexIndex & 2u) << 1u) - 1.0;

    output.position = vec4f(x, y, 0.0, 1.0);
    output.texCoord = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);

    return output;
}

@group(0) @binding(0) var cameraTexture: texture_2d<f32>;
@group(0) @binding(1) var cameraSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let hdr = textureSample(cameraTexture, cameraSampler, input.texCoord);

    // Reinhard tone map: compresses HDR > 1 back into [0,1] while keeping
    // shadow and midtone detail. The surface format is sRGB so the GPU
    // performs the linear -> sRGB encoding after this shader returns; we
    // must NOT apply a manual pow(1/2.2) on top or midtones get crushed.
    let mapped = hdr.rgb / (hdr.rgb + vec3f(1.0));
    return vec4f(mapped, hdr.a);
}
