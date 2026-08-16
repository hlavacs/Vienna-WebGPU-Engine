// Centralised tonemap. Every per-camera pass writes raw linear HDR into the
// intermediate target; this shader is the single place the curve is applied.

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) texCoord: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;

    let x = f32((vertexIndex & 1u) << 2u) - 1.0;
    let y = f32((vertexIndex & 2u) << 1u) - 1.0;

    output.position = vec4f(x, y, 0.0, 1.0);
    output.texCoord = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);

    return output;
}

@group(0) @binding(0) var cameraTexture: texture_2d<f32>;
@group(0) @binding(1) var cameraSampler: sampler;

// params: x = exposure, y = mode (0 = clamp, 1 = ACES Filmic), z/w reserved.
struct PostProcessUniforms {
    params: vec4f,
}

@group(1) @binding(0) var<uniform> uPost: PostProcessUniforms;

// ACES Filmic approximation (Krzysztof Narkowicz). Rolls bright HDR values
// off gracefully while keeping LDR-range inputs near 1:1; Reinhard compresses
// 1.0 -> 0.5 and made the scene look washed-out before we switched.
fn acesFilmic(x: vec3f) -> vec3f {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3f(0.0), vec3f(1.0));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let hdr = textureSample(cameraTexture, cameraSampler, input.texCoord);

    let exposed = hdr.rgb * uPost.params.x;

    // Surface format is sRGB so the GPU does linear -> sRGB encode after this
    // shader returns; do NOT apply a manual pow(1/2.2) or midtones get crushed.
    var mapped: vec3f;
    if (uPost.params.y < 0.5) {
        mapped = clamp(exposed, vec3f(0.0), vec3f(1.0));
    } else {
        mapped = acesFilmic(exposed);
    }

    return vec4f(mapped, hdr.a);
}
