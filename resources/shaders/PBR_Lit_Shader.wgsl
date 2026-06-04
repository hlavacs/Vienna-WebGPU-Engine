#include "engine://core/frame_uniforms.wgsl"

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec3f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) view_direction: vec3f,
    @location(4) tangent: vec3f,
    @location(5) bitangent: vec3f,
    @location(6) world_position: vec4f,
}

// @group(0) Frame already included at file top. @group(1) Scene + structs:
#include "engine://core/scene_bindings.wgsl"
#include "engine://core/object_uniforms.wgsl"
#include "engine://core/material.wgsl"

// AlphaMode enum values must mirror C++ engine::rendering::AlphaMode.
const ALPHA_MODE_OPAQUE: u32 = 0u;
const ALPHA_MODE_MASK:   u32 = 1u;
const ALPHA_MODE_BLEND:  u32 = 2u;

// 0 = ambient, 1 = directional, 2 = point, 3 = spot — values match
// engine::rendering::LightType.

// @group(2) Material — u_material from the codegen include above, plus the
// per-material texture slots below.
@group(2) @binding(1)
var texture_sampler: sampler;
@group(2) @binding(2)
var base_color_texture: texture_2d<f32>;
@group(2) @binding(3)
var normal_texture: texture_2d<f32>;
@group(2) @binding(4)
var ao_texture: texture_2d<f32>;
@group(2) @binding(5)
var roughness_texture: texture_2d<f32>;
@group(2) @binding(6)
var metallic_texture: texture_2d<f32>;
@group(2) @binding(7)
var emission_texture: texture_2d<f32>;

// @group(3) Object — u_object comes from the codegen include above.

const PI: f32 = 3.141592653589793;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let world_pos = u_object.modelMatrix * vec4f(in.position, 1.0);
    out.world_position = world_pos;
    out.position = u_frame.viewProjectionMatrix * world_pos;
    
    let N = normalize((u_object.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    let T = normalize((u_object.normalMatrix * vec4f(in.tangent.xyz, 0.0)).xyz);
    let B = cross(N, T) * in.tangent.w;
    out.normal = N;
    out.tangent = T;
    out.bitangent = B;

    out.color = in.color;
    out.uv = in.uv;
    out.view_direction = u_frame.cameraWorldPosition - world_pos.xyz;
    return out;
}

// ------------------------------------------------------------
// Utility — saturate / transform / equirect helpers live in lib/lighting.wgsl
// Shadow PCF / cascade / cube sampling lives in lib/shadow.wgsl
// ------------------------------------------------------------
#include "engine://lib/lighting.wgsl"
#include "engine://lib/shadow.wgsl"

// fresnel_schlick + fresnel_schlick_roughness now live in lib/lighting.wgsl
// so deferred_composition and the forward PBR shader use the exact same
// formula — IBL terms can't disagree between the two lighting paths.

fn distribution_ggx(n: vec3f, h: vec3f, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let n_dot_h = max(dot(n, h), 0.0);
    let n_dot_h2 = n_dot_h * n_dot_h;

    let denom = (n_dot_h2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom + 0.000001);
}

fn geometry_schlick_ggx(n_dot_v: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return n_dot_v / (n_dot_v * (1.0 - k) + k + 0.000001);
}

fn geometry_smith(n: vec3f, v: vec3f, l: vec3f, roughness: f32) -> f32 {
    let n_dot_v = max(dot(n, v), 0.0);
    let n_dot_l = max(dot(n, l), 0.0);
    let ggx1 = geometry_schlick_ggx(n_dot_v, roughness);
    let ggx2 = geometry_schlick_ggx(n_dot_l, roughness);
    return ggx1 * ggx2;
}

fn sample_environment_irradiance(normal: vec3f) -> vec3f {
    if (u_environment.params.x < 0.5) {
        return vec3f(0.0);
    }

    // Pre-convolved cosine-weighted irradiance map — radiometrically correct
    // diffuse IBL with no per-fragment work. The bake already applies the
    // PI / sampleCount normalization that the Lambertian BRDF expects, so
    // the consumer just multiplies by baseColor * (1 - metallic) * ao here
    // and by params.y as the intensity scalar.
    let uv = direction_to_equirect_uv(normalize(normal));
    return textureSample(irradiance_map, environment_sampler, uv).rgb
         * u_environment.params.y;
}

// Specular IBL sample (analog of sample_environment_irradiance but at the
// reflection vector, with roughness driving mip selection). Cheap split-sum
// approximation — the mip chain stands in for a pre-filtered cubemap.
//
// HDR sample clamped to the same ceiling as the diffuse path. Without
// prefiltering, leaving this unclamped lets sun spikes from the HDR
// equirect (values up to 10-50×) leak through Fresnel and bloom every
// pixel — even at F0=0.04 the contribution can dominate diffuse. Clamping
// loses real HDR highlights but keeps the engine usable until proper
// split-sum prefiltering lands.
fn sample_environment_reflection(reflect_dir: vec3f, roughness: f32) -> vec3f {
    if (u_environment.params.x < 0.5) {
        return vec3f(0.0);
    }

    let uv     = direction_to_equirect_uv(normalize(reflect_dir));
    let maxMip = max(0.0, f32(textureNumLevels(environment_texture)) - 1.0);
    let mip    = clamp(roughness, 0.0, 1.0) * maxMip;
    let raw    = textureSampleLevel(environment_texture, environment_sampler, uv, mip).rgb;
    return min(raw, vec3f(0.3)) * u_environment.params.y;
}

// ------------------------------------------------------------
// Shadow Mapping — select_cascade / calculate_shadow live in lib/shadow.wgsl
// ------------------------------------------------------------

// ------------------------------------------------------------
// Fragment
// ------------------------------------------------------------
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let base_sample = textureSample(base_color_texture, texture_sampler, in.uv);

    let base_color = base_sample.rgb * u_material.diffuse.rgb;
    var alpha = base_sample.a * u_material.diffuse.a;

    // Mask = binary cutout against the GLTF threshold; blend pipelines pass
    // alpha straight through to SrcAlpha blending; opaque pipelines ignore alpha.
    if (u_material.alphaMode == ALPHA_MODE_MASK) {
        if (alpha < u_material.alphaCutoff) {
            discard;
        }
        alpha = 1.0;
    }

    let n = normalize(in.normal);
    let t = normalize(in.tangent - n * dot(n, in.tangent));
    let b = cross(n, t);
    let tbn = mat3x3f(t, b, n);

    let n_map: vec3f = textureSample(normal_texture, texture_sampler, in.uv).rgb * 2.0 - 1.0;
    let scaled_n = clamp(n_map.xy * u_material.normalTextureScale, vec2<f32>(-2.0, -2.0), vec2<f32>(2.0, 2.0));
    let normal = normalize(tbn * vec3<f32>(scaled_n.x, scaled_n.y, n_map.z));

    let v = normalize(in.view_direction);
    let roughness = clamp(textureSample(roughness_texture, texture_sampler, in.uv).r * u_material.roughness, 0.001, 1.0);
    let metallic = textureSample(metallic_texture, texture_sampler, in.uv).r * u_material.metallic;
    let ao_tex = textureSample(ao_texture, texture_sampler, in.uv).r;
    let ao = saturate(ao_tex);
    let emission = textureSample(emission_texture, texture_sampler, in.uv).rgb * u_material.emission.rgb * u_material.emission.w;

    let ior = max(u_material.ior, 1.0);
    let f0_dielectric = pow((ior - 1.0) / (ior + 1.0), 2.0);
    let f0 = mix(vec3f(f0_dielectric), base_color, metallic);

    var Lo = vec3f(0.0);
    let world_pos = in.world_position.xyz;

    for (var i: u32 = 0u; i < u_lights.count; i = i + 1u) {
        let light = u_lights.lights[i];
        if (light.light_type == 0u) {
            Lo += base_color * u_material.ambient.rgb * u_material.ambient.w * light.color * light.intensity * ao;
            continue;
        }

        var L = vec3f(0.0);
        var attenuation = 1.0;

        if (light.light_type == 1u) {
            L = get_direction_from_transform(light.transform);
        } else if (light.light_type == 2u) {
            let light_pos = get_position_from_transform(light.transform);
            let to_light = light_pos - world_pos;
            let dist = length(to_light);
            L = normalize(to_light);
            attenuation = 1.0 / max(dist * dist, 0.001);
        } else if (light.light_type == 3u) {
            let light_pos = get_position_from_transform(light.transform);
            let to_light = light_pos - world_pos;
            let dist = length(to_light);
            L = normalize(to_light);

            let spot_dir = get_direction_from_transform(light.transform);
            let cos_theta = dot(L, spot_dir);
            let inner_ratio = 1.0 - max(0.01, light.spot_softness);
            let cos_outer = cos(light.spot_angle);
            let cos_inner = cos(light.spot_angle * inner_ratio);
            let spot_effect = smoothstep(cos_outer, cos_inner, cos_theta);

            let dist_attenuation = 1.0 / max(dist * dist, 0.01);
            let range_factor = 1.0 - smoothstep(light.range * 0.75, light.range, dist);
            attenuation = select(0.0, spot_effect * dist_attenuation * range_factor, cos_theta > cos_outer);
        }

        let H = normalize(v + L);
        let n_dot_l = max(dot(normal, L), 0.0);
        if (n_dot_l <= 0.0) { continue; }
        let n_dot_v = max(dot(normal, v), 0.0);

        let shadow = calculate_shadow(world_pos, normal, light);

        let d = distribution_ggx(normal, H, roughness);
        let g = geometry_smith(normal, v, L, roughness);
        let f = fresnel_schlick(max(dot(H, v), 0.0), f0);

        let numerator = d * g * f;
        let denominator = max(4.0 * n_dot_v * n_dot_l, 0.001);
        let specular = numerator / denominator;

        let k_s = f;
        // Diffuse share reduced by transmission factor for the same energy-
        // conservation reason as the IBL block: light that goes through the
        // surface can't also bounce diffusely off it. Specular keeps full
        // weight because direct specular highlights look correct on glass.
        let k_d = (vec3f(1.0) - k_s) * (1.0 - metallic)
                * (1.0 - clamp(u_material.transmittance.a, 0.0, 1.0));

        let radiance = light.color * light.intensity * attenuation;
        Lo += (k_d * base_color / PI + specular) * radiance * n_dot_l * shadow;
    }

    // IBL ambient: split-sum diffuse + specular using the BRDF LUT and
    // GGX-prefiltered env mip chain. Matches the deferred composition's
    // IBL block so opaque and transparent surfaces stay visually
    // consistent across the deferred/forward boundary.
    let ibl_NdotV   = max(dot(normal, v), 0.0);
    let ibl_F       = fresnel_schlick_roughness(ibl_NdotV, f0, roughness);
    let ibl_kD      = (vec3f(1.0) - ibl_F) * (1.0 - metallic);
    let ibl_irrad   = sample_environment_irradiance(normal);
    let ibl_diffuse = ibl_kD * ibl_irrad * base_color * ao;

    let reflect_dir = reflect(-v, normal);
    let prefiltered_uv = direction_to_equirect_uv(reflect_dir);
    let max_mip = max(0.0, f32(textureNumLevels(prefiltered_env)) - 1.0);
    let env_spec = textureSampleLevel(prefiltered_env, environment_sampler, prefiltered_uv, roughness * max_mip).rgb;
    let env_brdf = textureSample(brdf_lut, environment_sampler, vec2f(ibl_NdotV, roughness)).rg;
    let ibl_specular = env_spec * (ibl_F * env_brdf.x + env_brdf.y) * u_environment.params.y * ao;

    let final_color  = Lo + ibl_diffuse + ibl_specular + emission;

    // Raw linear HDR. The blend stage (SrcAlpha / OneMinusSrcAlpha for Transparent
    // pipelines) uses the returned alpha; no fake glass refraction is folded in
    // because that double-counted lighting and made water / glass look too bright.
    return vec4f(final_color, alpha);
}
