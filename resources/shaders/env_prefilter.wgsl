// Environment-map pre-filter for the split-sum IBL specular approximation.
//
// Runs once per output mip level: takes the source equirect HDR, importance-
// samples GGX(roughness) directions, accumulates weighted env samples into
// the destination texel. The result mip chain is sampled at render time as
//   textureSampleLevel(prefiltered, smp, uv, roughness * maxMip)
// to get the GGX-convolved environment for that surface roughness.
//
// Pair with the BRDF integration LUT (resources/shaders/brdf_lut.wgsl) to
// reconstruct the full split-sum integral:
//   spec = envSample * (F * envBRDF.x + envBRDF.y)
//
// Reference: "Real Shading in Unreal Engine 4" (Karis 2013).

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0)       uv:       vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOutput {
    let uv = vec2<f32>(
        f32((vid << 1u) & 2u),
        f32(vid & 2u)
    );
    var out: VertexOutput;
    out.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
    out.uv = uv;
    return out;
}

const PI:           f32 = 3.14159265358979323846;
const SAMPLE_COUNT: u32 = 1024u;

// Per-mip uniform: .x = roughness for this mip level (0..1), .y = source env
// max mip (for the lower-mip sampling trick), other lanes reserved.
struct PrefilterParams {
    params: vec4<f32>,
}

// @standalone-shader — built its own pipeline layout in PrefilteredEnv.cpp,
// never bound through the engine's Frame/Scene/Material/Object convention.
// The marker tells the WGSL reflector to skip the canonical bind-group
// validation; without it the validator would (correctly, in general)
// complain that @group(0) collides with the engine-reserved range.
@group(0) @binding(0) var          srcEnv:    texture_2d<f32>;
@group(0) @binding(1) var          srcSmp:    sampler;
@group(0) @binding(2) var<uniform> u_params:  PrefilterParams;

// Equirect UV → direction on the unit sphere. Inverse of
// direction_to_equirect_uv used elsewhere in the engine.
fn equirectUvToDir(uv: vec2<f32>) -> vec3<f32> {
    let phi   = (uv.x - 0.5) * 2.0 * PI;
    let theta = uv.y * PI;
    let sinT  = sin(theta);
    return vec3<f32>(sinT * cos(phi), cos(theta), sinT * sin(phi));
}

fn directionToEquirectUv(direction: vec3<f32>) -> vec2<f32> {
    let dir = normalize(direction);
    let u = atan2(dir.z, dir.x) * (0.5 / PI) + 0.5;
    let v = acos(clamp(dir.y, -1.0, 1.0)) / PI;
    return vec2<f32>(u, v);
}

// Hammersley low-discrepancy sequence — same impl as brdf_lut.wgsl,
// duplicated here because WGSL doesn't have cross-file fn imports yet.
fn radicalInverseVdC(n_in: u32) -> f32 {
    var n: u32 = n_in;
    n = (n << 16u) | (n >> 16u);
    n = ((n & 0x55555555u) << 1u)  | ((n & 0xAAAAAAAAu) >> 1u);
    n = ((n & 0x33333333u) << 2u)  | ((n & 0xCCCCCCCCu) >> 2u);
    n = ((n & 0x0F0F0F0Fu) << 4u)  | ((n & 0xF0F0F0F0u) >> 4u);
    n = ((n & 0x00FF00FFu) << 8u)  | ((n & 0xFF00FF00u) >> 8u);
    return f32(n) * 2.3283064365386963e-10;
}

fn hammersley(i: u32, n: u32) -> vec2<f32> {
    return vec2<f32>(f32(i) / f32(n), radicalInverseVdC(i));
}

// GGX importance-sampled half-vector in tangent space, transformed to world.
fn importanceSampleGGX(xi: vec2<f32>, n: vec3<f32>, roughness: f32) -> vec3<f32> {
    let a = roughness * roughness;
    let phi = 2.0 * PI * xi.x;
    let cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    let sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    let h = vec3<f32>(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );

    let up      = select(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 0.0, 1.0), abs(n.z) < 0.999);
    let tangent = normalize(cross(up, n));
    let bitan   = cross(n, tangent);

    return normalize(tangent * h.x + bitan * h.y + n * h.z);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Split-sum simplification: take N == R == V. This is the same shortcut
    // UE4 uses — exact at mirror roughness, biased at high roughness but
    // visually acceptable.
    //
    // V-axis flip: WebGPU rasterizes vs.uv.y=0 to texel row H-1 while
    // runtime textureSample reads uv.y=0 from row 0. Without the inversion
    // the prefiltered env is stored upside-down — a reflection pointing up
    // would sample the down-direction's GGX-convolved env, producing a
    // mirrored sky/ground swap on every reflective surface.
    let N = equirectUvToDir(vec2<f32>(in.uv.x, 1.0 - in.uv.y));
    let R = N;
    let V = R;

    let roughness = clamp(u_params.params.x, 0.0, 1.0);
    var color:  vec3<f32> = vec3<f32>(0.0);
    var weight: f32       = 0.0;

    // At roughness 0 the integral degenerates to a single mirror sample.
    // Skip the importance-sample loop in that case — both faster and avoids
    // the GGX kernel collapsing to a delta function.
    if (roughness < 0.001) {
        let uv = directionToEquirectUv(R);
        return vec4<f32>(textureSampleLevel(srcEnv, srcSmp, uv, 0.0).rgb, 1.0);
    }

    for (var i: u32 = 0u; i < SAMPLE_COUNT; i = i + 1u) {
        let xi = hammersley(i, SAMPLE_COUNT);
        let H  = importanceSampleGGX(xi, N, roughness);
        let L  = normalize(2.0 * dot(V, H) * H - V);

        let NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            let uv     = directionToEquirectUv(L);
            // Sample mip 0 of the source — caller pins the env texture as
            // single-mip for the prefilter pass.
            let sample = textureSampleLevel(srcEnv, srcSmp, uv, 0.0).rgb;
            color  += sample * NdotL;
            weight += NdotL;
        }
    }

    return vec4<f32>(color / weight, 1.0);
}
