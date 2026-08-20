// Tutorial 03 - Glass with Reflections, Transparency and Shadows
// Follow the guide in doc/tutorials/03_glass_shader.md
//
// Unlike Tutorial 01 this file ships complete - the tutorial walks through it
// section by section instead of making you type it in. The "Step N" markers
// below match the steps in the guide.
//
// What this shader demonstrates:
// - Transparency (low base opacity, blended in the forward pass)
// - The real Fresnel split between reflection and transmission (Schlick, F0 = 0.04)
// - Environment reflections sampled from the engine's prefiltered env map
// - A simple Blinn-Phong specular highlight from the directional light
// - RECEIVED shadows via the engine's Scene bind group - the same
//   calculate_shadow() the PBR shader uses (cascades, bias, PCF)

// Tutorial 03 - Step 2: Engine bind groups via #include
//
// Frame (@group(0)) and Object (@group(3)) are the same includes as in
// Tutorials 01/02. New here is the Scene group (@group(1)): it carries the
// light list, the shadow map arrays, the comparison sampler, the per-shadow
// uniforms - and the environment textures, including the GGX-prefiltered
// reflection map (prefiltered_env, binding 11) this shader samples in Step 9.
// Including it is ALL the wiring shadows and reflections need on the shader
// side - the engine fills and binds the Scene group for every forward-pass draw.
#include "engine://core/frame_uniforms.wgsl"
#include "engine://core/scene_bindings.wgsl"
#include "engine://core/object_uniforms.wgsl"

// Shared shadow sampling. lib/shadow.wgsl provides calculate_shadow(), the
// exact code path PBR_Lit_Shader.wgsl and the deferred composition use -
// reusing it means glass shadows can never drift from the rest of the scene.
// It pulls in lib/lighting.wgsl itself (the include resolver deduplicates),
// which is where direction_to_equirect_uv() for Step 9 comes from.
#include "engine://lib/shadow.wgsl"

// Tutorial 03 - Step 3: Vertex structures
//
// Glass needs the WORLD-space position and normal in the fragment shader:
// the view vector (fresnel + reflection), the light direction (specular)
// and the shadow lookup all operate in world space.
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) texCoord: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPosition: vec3f,
    @location(1) worldNormal: vec3f,
}

// Tutorial 03 - Step 4: Glass material uniforms at @group(2)
//
// Hand-declared like the unlit material in Tutorial 01. The C++ side feeds
// this buffer from the GlassProperties struct in main.cpp - the two must
// match byte for byte (32 bytes: two vec4f).
struct GlassMaterialUniforms {
    // rgb = glass tint, a = base opacity when looking straight through
    // (LOW - real glass transmits most of the background head-on)
    color: vec4f,
    // x = reflection strength (scales the sampled environment reflection),
    // y = specular shininess (Blinn-Phong exponent), z = specular strength,
    // w = shadow dimming (0 = shadows invisible, 1 = shadowed glass goes
    // fully dark)
    params: vec4f,
}

@group(2) @binding(0)
var<uniform> glassMaterialUniforms: GlassMaterialUniforms;

// Reflectance of glass at normal incidence: a dielectric with an index of
// refraction around 1.5 reflects ~4% of light head-on. The same constant
// the PBR pipeline uses as its dielectric F0.
const GLASS_F0: f32 = 0.04;

// Tutorial 03 - Step 5: Vertex shader
//
// Same matrix pipeline as Tutorial 01, but we keep the intermediate
// world-space results and hand them to the fragment stage. The normal goes
// through the normal matrix (not the model matrix) so non-uniform scaling
// cannot bend it away from the true surface direction.
@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    let worldPosition = u_object.modelMatrix * vec4f(input.position, 1.0);
    output.position = u_frame.viewProjectionMatrix * worldPosition;
    output.worldPosition = worldPosition.xyz;
    // w = 0.0 marks a direction: directions must not receive translation.
    output.worldNormal = normalize((u_object.normalMatrix * vec4f(input.normal, 0.0)).xyz);

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Interpolation across the triangle denormalizes the normal, so we
    // re-normalize per fragment before using it in any dot product.
    let normal = normalize(input.worldNormal);

    // Tutorial 03 - Step 6: Fresnel - the reflection/transmission split
    //
    // The view vector points from the surface toward the camera. The Fresnel
    // term answers THE question that makes glass look like glass: how much of
    // the incoming light reflects off the surface versus passing through it?
    // Head-on (dot ~ 1) only ~4% reflects (F0) - the glass transmits. At
    // grazing angles (dot ~ 0) reflectance climbs toward 100% - the surface
    // becomes a mirror. Schlick's fifth-power falloff approximates that ramp
    // cheaply; it is the scalar form of the fresnel_schlick() helper the PBR
    // pipeline uses (lib/lighting.wgsl).
    let viewDirection = normalize(u_frame.cameraWorldPosition - input.worldPosition);
    let facingAmount = max(dot(normal, viewDirection), 0.0);
    let fresnel = GLASS_F0 + (1.0 - GLASS_F0) * pow(1.0 - facingAmount, 5.0);

    // Tutorial 03 - Step 7: Find the directional light for the specular highlight
    //
    // The Scene group's light list holds every light in the scene. Glass only
    // needs the sun: we scan for the first directional light (light_type 1)
    // instead of accumulating all lights like the PBR shader does - a
    // deliberate simplification that keeps the focus on the glass effects.
    var lightDirection = vec3f(0.0, 1.0, 0.0);
    var lightRadiance = vec3f(0.0);
    var shadowVisibility = 1.0;
    for (var i = 0u; i < u_lights.count; i = i + 1u) {
        // The lights array is runtime-sized; guard the index so a count that
        // outruns the buffer can never read out of bounds.
        if (i >= arrayLength(&u_lights.lights)) {
            break;
        }
        let light = u_lights.lights[i];
        if (light.light_type != 1u) {
            continue;
        }

        // get_direction_from_transform() (lib/lighting.wgsl) returns the
        // vector TOWARD the light - the convention every engine lighting
        // helper shares.
        lightDirection = get_direction_from_transform(light.transform);
        lightRadiance = light.color * light.intensity;

        // Tutorial 03 - Step 8: Received shadows
        //
        // calculate_shadow() (lib/shadow.wgsl) returns 1.0 when this world
        // position sees the light and 0.0 when it is occluded. Internally it
        // selects the correct CSM cascade, projects the position into light
        // space, applies slope-scaled depth bias against shadow acne, and
        // averages a PCF kernel of hardware depth-comparison samples for
        // soft edges. Reusing it is why this shader gets the exact same
        // shadows as the PBR objects around it.
        shadowVisibility = calculate_shadow(input.worldPosition, normal, light);
        break;
    }

    // Blinn-Phong specular: the half vector sits between view and light
    // direction; the closer the normal aligns with it, the tighter the
    // mirror-like highlight. Cheaper than the PBR GGX lobe and easier to
    // reason about, which is why it is the classic teaching model.
    // Multiplying by shadowVisibility removes the highlight in shadow -
    // a specular glint needs a direct line to the light.
    let halfVector = normalize(viewDirection + lightDirection);
    let specular = pow(max(dot(normal, halfVector), 0.0), glassMaterialUniforms.params.y)
        * glassMaterialUniforms.params.z
        * shadowVisibility;

    // Tutorial 03 - Step 9: Environment reflection
    //
    // A specular glint alone is not enough - real glass mirrors its
    // surroundings. reflect() bounces the incoming view ray off the surface
    // (note the minus: reflect() wants the vector FROM the camera), and
    // direction_to_equirect_uv() (lib/lighting.wgsl) converts that world
    // direction into UVs in the engine's equirectangular environment map.
    //
    // prefiltered_env (Scene group, binding 11) is the environment map
    // GGX-prefiltered into a mip chain: mip 0 is the sharp original, higher
    // mips are progressively blurrier for rough surfaces. Polished glass is
    // perfectly smooth, so we sample mip 0 with textureSampleLevel - the
    // exact pattern the PBR shader's IBL specular path uses, minus the
    // roughness-driven mip selection.
    let reflectionDirection = reflect(-viewDirection, normal);
    let reflectionUV = direction_to_equirect_uv(reflectionDirection);
    let environmentReflection = textureSampleLevel(prefiltered_env, environment_sampler, reflectionUV, 0.0).rgb;

    // Tutorial 03 - Step 10: Compose transparency
    //
    // Shadow dimming: fully lit keeps the glass's own contribution at full
    // brightness, fully shadowed scales it down by (1 - shadowDimming).
    // Glass in shadow still transmits the background, so we dim the tint and
    // the reflection instead of blacking out.
    let litFactor = mix(1.0 - glassMaterialUniforms.params.w, 1.0, shadowVisibility);

    // The fresnel term is an energy split: what reflects cannot transmit.
    // The tint (the transmitted side) fades toward the silhouette exactly as
    // the reflection (the reflected side) takes over - the same kS/kD split
    // the PBR shader applies between its specular and diffuse lobes.
    let bodyColor = glassMaterialUniforms.color.rgb * litFactor * (1.0 - fresnel);
    let reflectionColor = environmentReflection * glassMaterialUniforms.params.x * fresnel * litFactor;
    let specularColor = lightRadiance * specular;

    // Alpha drives the SrcAlpha / OneMinusSrcAlpha blend the engine enables
    // for Transparent materials. The LOW base opacity keeps the center
    // see-through; fresnel raises opacity toward the rim (grazing angles
    // reflect more, so less background shows through) and the specular term
    // keeps the glint from being washed out by the blend.
    let alpha = clamp(glassMaterialUniforms.color.a + fresnel + specular, 0.0, 1.0);

    return vec4f(bodyColor + reflectionColor + specularColor, alpha);
}
