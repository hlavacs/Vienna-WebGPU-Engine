// Diffuse irradiance convolution — one-shot fullscreen pass that integrates
// the source equirect HDR over a Lambertian (cosine-weighted) hemisphere
// for every output texel. The result is the radiometrically-correct env
// diffuse term: at render time, the diffuse IBL tap becomes a single
// textureSample at the surface normal direction with no further math.
//
// Pair with env_prefilter.wgsl (GGX specular convolution) and brdf_lut.wgsl
// (split-sum F integration) to complete the IBL trifecta.
//
// Reference: "Image Based Lighting" — http://www.codinglabs.net/article_physically_based_rendering.aspx
// and the Karis "Real Shading in Unreal Engine 4" (2013) integral.

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

const PI: f32 = 3.14159265358979323846;

// Hemisphere sample step sizes for the Riemann-sum form of the cosine-
// weighted integral. 0.025 phi + 0.025 theta gives 251×62 ≈ 15.6k samples
// per output texel — heavy but only runs once per env load (we cache the
// result texture), and matches what the LearnOpenGL / Karis references use.
const PHI_STEP:   f32 = 0.025;
const THETA_STEP: f32 = 0.025;

// This shader is a self-contained one-shot bake — it builds its own pipeline
// layout in IrradianceMap.cpp and never goes through the engine's
// Frame/Scene/Material/Object convention. Same warning the validator gives
// for env_prefilter.wgsl applies here.
@group(0) @binding(0) var srcEnv: texture_2d<f32>;
@group(0) @binding(1) var srcSmp: sampler;

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

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Treat the output texel's equirect direction as the surface normal.
    // At render time, the consumer sampler maps normal → uv → this texel.
    let N = equirectUvToDir(in.uv);

    // Build an orthonormal basis around N so we can walk a hemisphere
    // aligned to it. `up` picks a non-degenerate ref vector; up × N gives
    // a tangent perpendicular to N.
    let up_ref = select(vec3<f32>(0.0, 1.0, 0.0),
                        vec3<f32>(1.0, 0.0, 0.0),
                        abs(N.y) > 0.999);
    let right  = normalize(cross(up_ref, N));
    let up     = cross(N, right);

    var irradiance: vec3<f32> = vec3<f32>(0.0);
    var sampleCount: f32      = 0.0;

    // Riemann sum over (phi, theta) — phi sweeps around N, theta from 0 to
    // PI/2 (hemisphere). `cosT * sinT` is the Lambertian + Jacobian factor
    // that makes this a proper cosine-weighted hemispherical average.
    var phi: f32 = 0.0;
    loop {
        if (phi >= 2.0 * PI) { break; }

        var theta: f32 = 0.0;
        loop {
            if (theta >= 0.5 * PI) { break; }

            let sinT = sin(theta);
            let cosT = cos(theta);

            // Tangent-space sample → world-space direction. The hemisphere
            // is centered on +Z in tangent space; transform via (right, up, N).
            let tangentSample = vec3<f32>(sinT * cos(phi), sinT * sin(phi), cosT);
            let worldSample   = tangentSample.x * right
                              + tangentSample.y * up
                              + tangentSample.z * N;

            let uv     = directionToEquirectUv(worldSample);
            let radiance = textureSampleLevel(srcEnv, srcSmp, uv, 0.0).rgb;

            // Multiply by cosT to weight by NdotL (Lambertian) and sinT to
            // account for the surface-area element on the unit hemisphere.
            irradiance  += radiance * cosT * sinT;
            sampleCount += 1.0;

            theta += THETA_STEP;
        }

        phi += PHI_STEP;
    }

    // Multiply by PI / sampleCount — the discrete form of the radiometric
    // integral. The leading PI cancels the 1/PI in the diffuse BRDF the
    // consumer applies, so the result can plug straight into the shading
    // path as `irradiance * baseColor * (1 - metallic) * ao`.
    irradiance = PI * irradiance * (1.0 / sampleCount);

    return vec4<f32>(irradiance, 1.0);
}
