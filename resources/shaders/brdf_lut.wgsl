// BRDF integration LUT — fullscreen pass that bakes the split-sum
// approximation's (scale, bias) Fresnel terms into a 2D texture indexed by
// (NdotV, roughness). One-shot at engine init; the result feeds the IBL
// specular term in the deferred composition + PBR forward shaders.
//
// Reference: "Real Shading in Unreal Engine 4" (Karis 2013), the
// PreFilterEnvironmentMap + integrateBRDF derivation.
//
// Output is RG16Float: .r = scale on F0, .g = bias added directly. The
// caller's IBL specular path then does:
//   `let envBRDF = textureSample(brdfLut, sampler, vec2(NdotV, roughness)).rg;`
//   `let spec    = envSample * (F0 * envBRDF.x + envBRDF.y);`

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0)       uv:       vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOutput {
    // Standard fullscreen-triangle trick. uv is (0..1) for both axes — the
    // x maps to NdotV, the y to roughness when sampled later.
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
const SAMPLE_COUNT: u32 = 1024u;

// Radical-inverse base 2 — Hammersley low-discrepancy sequence component.
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

// GGX importance-sampled half-vector in tangent space.
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

    // Tangent → world. Pick an arbitrary up vector that isn't parallel to n.
    let up      = select(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 0.0, 1.0), abs(n.z) < 0.999);
    let tangent = normalize(cross(up, n));
    let bitan   = cross(n, tangent);

    return normalize(tangent * h.x + bitan * h.y + n * h.z);
}

// Geometry term: Smith's height-correlated GGX, ggxIBL flavour (IBL uses
// a slightly different k than direct lighting — see Karis 2013).
fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let a = roughness;
    let k = (a * a) * 0.5;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N: vec3<f32>, V: vec3<f32>, L: vec3<f32>, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

fn integrateBRDF(NdotV: f32, roughness: f32) -> vec2<f32> {
    let V = vec3<f32>(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    var scale: f32 = 0.0;
    var bias:  f32 = 0.0;
    let N = vec3<f32>(0.0, 0.0, 1.0);

    for (var i: u32 = 0u; i < SAMPLE_COUNT; i = i + 1u) {
        let xi = hammersley(i, SAMPLE_COUNT);
        let H  = importanceSampleGGX(xi, N, roughness);
        let L  = normalize(2.0 * dot(V, H) * H - V);

        let NdotL = max(L.z, 0.0);
        let NdotH = max(H.z, 0.0);
        let VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            let G    = geometrySmith(N, V, L, roughness);
            let Gvis = (G * VdotH) / (NdotH * NdotV);
            let Fc   = pow(1.0 - VdotH, 5.0);

            scale += (1.0 - Fc) * Gvis;
            bias  += Fc * Gvis;
        }
    }

    return vec2<f32>(scale, bias) / f32(SAMPLE_COUNT);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec2<f32> {
    // Clamp away from exact zero NdotV to keep the integration stable —
    // V at NdotV=0 sits in the surface plane and the half-vector path
    // produces NaNs.
    let NdotV     = max(in.uv.x, 0.001);
    let roughness = in.uv.y;
    return integrateBRDF(NdotV, roughness);
}
