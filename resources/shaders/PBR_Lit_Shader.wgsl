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
#include "engine://lib/direct_lighting.wgsl"
#include "engine://lib/clustering.wgsl"

// fresnel_schlick + fresnel_schlick_roughness + the Cook-Torrance GGX terms
// (distribution / geometry) now live in lib/lighting.wgsl, and the per-light
// evaluation in lib/direct_lighting.wgsl. deferred_composition and this forward
// shader call the exact same code, so the two lighting paths can't disagree.

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

    // Clustered light lookup — identical froxel mapping to the deferred
    // composition so a transparent fragment is lit by the SAME set of lights as
    // the opaque surface behind it. Previously this loop scanned every light in
    // the scene, so transparent surfaces over-brightened and washed out grey as
    // the light count climbed. Reconstruct the screen UV from clip space (NDC
    // +Y up, matching the cluster compute) and view-space depth from the view
    // matrix — no screen-resolution uniform needed.
    let clip = u_frame.viewProjectionMatrix * vec4f(world_pos, 1.0);
    let cluster_uv = (clip.xy / clip.w) * 0.5 + vec2f(0.5);
    let view_depth = -(u_frame.viewMatrix * vec4f(world_pos, 1.0)).z;
    let cluster_idx = getClusterIndex(cluster_uv, view_depth);
    let cluster_lights = u_cluster_grid[cluster_idx];
    let light_count = min(cluster_lights.count, MAX_LIGHTS_PER_CLUSTER);

    for (var i: u32 = 0u; i < light_count; i = i + 1u) {
        let light_index_offset = cluster_lights.offset + i;
        if (light_index_offset >= arrayLength(&u_cluster_light_indices)) { break; }
        let light_idx = u_cluster_light_indices[light_index_offset];
        if (light_idx >= 5120u) { break; }
        let light = u_lights.lights[light_idx];

        if (light.light_type == 0u) {
            Lo += base_color * u_material.ambient.rgb * u_material.ambient.w * light.color * light.intensity * ao;
            continue;
        }

        // Single source of truth in lib/direct_lighting.wgsl (shared with the
        // deferred composition). transmittance.a removes the matching share of
        // the diffuse lobe for see-through dielectrics like water / glass.
        let shadow = calculate_shadow(world_pos, normal, light);
        Lo += evaluate_direct_light_ggx(
            light, normal, v, world_pos, base_color, f0, roughness, metallic,
            u_material.transmittance.a
        ) * shadow;
    }

    // IBL ambient: split-sum diffuse + specular using the BRDF LUT and
    // GGX-prefiltered env mip chain. Matches the deferred composition's
    // IBL block so opaque and transparent surfaces stay visually
    // consistent across the deferred/forward boundary.
    //
    // Block gated on params.x (irradianceEnabled). sample_environment_irradiance
    // already self-gates but the specular path samples prefiltered_env
    // directly — without the guard, the IBL specular leaks through when the
    // UI toggle says "off".
    var ibl_diffuse  = vec3f(0.0);
    var ibl_specular = vec3f(0.0);
    var ibl_F        = vec3f(0.0);
    let ibl_NdotV    = max(dot(normal, v), 0.0);
    if (u_environment.params.x > 0.5) {
        ibl_F           = fresnel_schlick_roughness(ibl_NdotV, f0, roughness);
        let ibl_kD      = (vec3f(1.0) - ibl_F) * (1.0 - metallic);
        let ibl_irrad   = sample_environment_irradiance(normal);
        ibl_diffuse     = ibl_kD * ibl_irrad * base_color * ao;

        // IBL_SPEC_SCALE matches deferred_composition.wgsl — see the note
        // there (split-sum bias term keeps a non-zero specular floor at
        // high roughness, this brings stone/concrete back down).
        let IBL_SPEC_SCALE: f32 = 0.15;
        let reflect_dir = reflect(-v, normal);
        let prefiltered_uv = direction_to_equirect_uv(reflect_dir);
        let max_mip = max(0.0, f32(textureNumLevels(prefiltered_env)) - 1.0);
        let env_spec = textureSampleLevel(prefiltered_env, environment_sampler, prefiltered_uv, roughness * max_mip).rgb;
        let env_brdf = textureSample(brdf_lut, environment_sampler, vec2f(ibl_NdotV, roughness)).rg;
        ibl_specular = env_spec * (ibl_F * env_brdf.x + env_brdf.y) * u_environment.params.y * ao * IBL_SPEC_SCALE;
    }

    // Body contributions (diffuse from direct + IBL, plus emission) are
    // modulated by transmittance: when the gltf alpha is 0.5 only half
    // the body is visible, the other half transmits the background.
    let body = Lo + ibl_diffuse + emission;

    // Fresnel-correct semi-transparent dielectric output for SrcAlpha blend.
    // Derivation: we want the post-blend pixel to be
    //   F · env + (1−F) · [α · body + (1−α) · bg]
    // SrcAlpha blend gives  src.rgb · src.a + bg · (1 − src.a),  so
    //   src.a   = 1 − (1−F)(1−α) = α + F · (1−α)
    //   src.rgb = (ibl_specular + α · body) / src.a
    // ibl_specular already contains the F factor (it's env · (F·scale + bias)),
    // so we use the scalar Fresnel weight F_avg only for the alpha boost.
    // For fully-opaque surfaces (α = 1) this degenerates to (body + spec, 1) —
    // no change to opaque-material rendering.
    let F_avg = (ibl_F.r + ibl_F.g + ibl_F.b) * (1.0 / 3.0);
    let effective_alpha = clamp(alpha + (1.0 - alpha) * F_avg, alpha, 1.0);
    let blended_rgb = (ibl_specular + alpha * body) / max(effective_alpha, 0.001);

    return vec4f(blended_rgb, effective_alpha);
}
