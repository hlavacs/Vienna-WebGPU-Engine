// Deferred Composition Shader
// Reads G-buffer data and performs PBR lighting using clustered lights.

// @group(0) Frame — FrameUniforms u_frame.
#include "engine://core/frame_uniforms.wgsl"

// @group(1) Scene — consolidated scene resources, struct definitions + 10
// binding declarations all bundled. Same include PBR forward uses.
#include "engine://core/scene_bindings.wgsl"

// @group(4) Custom — GBuffer textures (per-camera, pass-specific).
// RGBA16Float in C++ (alpha unused) for the emission target. Emission is added
// post-lighting so emissive surfaces (sky-domes, neon, eyes, magic) survive
// the deferred "albedo * lighting" multiplication that would otherwise zero
// them out when base color is dark.
@group(4) @binding(0) var gBufferPositionTexture: texture_2d<f32>;
@group(4) @binding(1) var gBufferNormalTexture: texture_2d<f32>;
@group(4) @binding(2) var gBufferAlbedoTexture: texture_2d<f32>;
@group(4) @binding(3) var gBufferMaterialTexture: texture_2d<f32>;
@group(4) @binding(4) var gBufferEmissionTexture: texture_2d<f32>;

// PBRProperties intentionally not declared — composition samples the
// G-buffer (RGBA packed material data) rather than the per-material UBO.

// Sample the cosine-weighted diffuse irradiance map at a world-space
// direction (typically the surface normal). The map is baked once per env
// change with the proper Lambertian convolution (PI / sampleCount), so the
// consumer just multiplies by baseColor * (1 - metallic) * ao — no extra
// PI division, no clamping, no hand-rolled hemisphere walk per fragment.
fn sampleEnvironmentAtDirection(direction: vec3<f32>) -> vec3<f32> {
	let uv = direction_to_equirect_uv(direction);
	return textureSample(irradiance_map, environment_sampler, uv).rgb;
}

// Vertex shader pass-through
struct VertexOutput {
	@builtin(position) position: vec4<f32>,
	@location(0) uv: vec2<f32>,
}

// Vertex shader: Full-screen quad
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
	var output: VertexOutput;
	// Validator-safe fullscreen triangle generation (no dynamic array indexing).
	let uv = vec2<f32>(
		f32((vertexIndex << 1u) & 2u),
		f32(vertexIndex & 2u)
	);
	output.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
	output.uv = uv;
	return output;
}

// getClusterIndex + the CLUSTER_* constants are shared with the forward PBR
// path via lib/clustering.wgsl, so opaque and transparent fragments map to the
// same froxel. depth fed in is VIEW-SPACE depth in world units (positionData.w
// from the g-buffer), not NDC depth.
#include "engine://lib/clustering.wgsl"

// Shared shadow + lighting math — single source for both PBR forward and
// deferred composition.
#include "engine://lib/lighting.wgsl"
#include "engine://lib/shadow.wgsl"
#include "engine://lib/direct_lighting.wgsl"

fn toneMapACES(color: vec3<f32>) -> vec3<f32> {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

// Fragment shader: PBR composition
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
	// Y is flipped ONLY for the G-buffer texture sample (gbuffer textures are
	// stored y-down). The cluster lookup must use the un-flipped uv to match
	// the NDC convention the compute shader writes with - otherwise lights
	// land in vertically mirrored cells from the reads and every pixel sees
	// count=0 even when there are lights right above them.
	let uvFlipped = vec2<f32>(input.uv.x, 1.0 - input.uv.y);
	let uv = clamp(uvFlipped, vec2<f32>(0.0, 0.0), vec2<f32>(0.999999, 0.999999));
	let uvCluster = clamp(input.uv, vec2<f32>(0.0, 0.0), vec2<f32>(0.999999, 0.999999));
	let texSize = textureDimensions(gBufferPositionTexture, 0);
	let pixelCoord = vec2<i32>(
		i32(uv.x * f32(texSize.x)),
		i32(uv.y * f32(texSize.y))
	);
	
	// Read G-buffers with integer texel fetch to support unfilterable float formats.
	let positionData = textureLoad(gBufferPositionTexture, pixelCoord, 0);
	let normalData = textureLoad(gBufferNormalTexture, pixelCoord, 0);
	let albedo = textureLoad(gBufferAlbedoTexture, pixelCoord, 0);
	let emissionData = textureLoad(gBufferEmissionTexture, pixelCoord, 0);

	// G-buffer albedo is RGBA8UnormSrgb: textureLoad already decodes sRGB ->
	// linear. Do NOT apply pow(2.2) here - that double-decodes and crushes
	// midtones (this was the cause of the "flat" cobblestone look).
	let albedoLinear = clamp(albedo.xyz, vec3<f32>(0.0), vec3<f32>(1.0));
	let materialData = textureLoad(gBufferMaterialTexture, pixelCoord, 0);

	let worldPos = positionData.xyz;
	let worldNormal = normalize(normalData.xyz);
	let roughness = materialData.x;
	let metallic = materialData.y;
	let ao = clamp(materialData.z, 0.0, 1.0);
	// materialData.w = materialType id (0 = standard PBR). Reserved for the
	// future data-reinterpretation deferred design - ignored today.
	let emission = emissionData.rgb;
	// position.w is VIEW-SPACE depth in world units (gbuffer wrote -viewPos.z).
	let viewDepth = positionData.w;
	let viewDir = normalize(u_frame.cameraWorldPosition - worldPos);

	// Trust the cluster compute - count=0 means no direct light affects this
	// pixel and we just emit IBL + emission. The old "scan all 512 lights as
	// a fallback when count=0" path defeated the entire optimization for
	// every pixel outside the lit volume.
	let clusterIdx = getClusterIndex(uvCluster, viewDepth);
	let clusterLights = u_cluster_grid[clusterIdx];
	let lightCount = min(clusterLights.count, 256u);

	// IBL ambient: diffuse from env-along-normal (clamped sample), specular
	// from the proper split-sum approximation using the GGX-prefiltered env
	// mip chain + BRDF integration LUT. Both gated on
	// u_environment.params.x; intensity from u_environment.params.y.
	//
	// Energy conservation: F is the Fresnel reflectance at NdotV (with the
	// roughness-aware Schlick variant from lib/lighting); (1 - F) is the
	// diffuse share, (1 - metallic) kills diffuse on metals so they only
	// reflect the env via the specular path.
	var finalColor = vec3<f32>(0.0);
	if (u_environment.params.x > 0.5) {
		let F0    = mix(vec3<f32>(0.04), albedoLinear, metallic);
		let NdotV = max(dot(worldNormal, viewDir), 0.0);
		let F     = fresnel_schlick_roughness(NdotV, F0, roughness);
		let kD    = (vec3<f32>(1.0) - F) * (1.0 - metallic);

		let envIrradiance = sampleEnvironmentAtDirection(worldNormal);
		finalColor += kD * albedoLinear * envIrradiance * u_environment.params.y * ao;

		// Specular IBL via split-sum: prefiltered env sample at roughness *
		// maxMip combined with the BRDF LUT's (scale, bias) Fresnel terms.
		// textureNumLevels gives the actual baked mip count so this stays
		// correct if PrefilteredEnv::MIP_LEVELS changes.
		//
		// IBL_SPEC_SCALE dampens specular relative to diffuse + skybox. The
		// split-sum bias term keeps a non-zero specular floor even at
		// roughness=1, so matte dielectrics in a bright HDR sky still read
		// as "lightly shiny" — fine for plastic, wrong for stone / concrete.
		// 0.15× brings the rough-material floor down to where stone reads
		// as matte while smoother surfaces still pick up a recognisable
		// reflection. Tuned against the SeaKeep demo's outdoor HDR; if a
		// scene with a darker env loses too much specular punch, promote
		// this to a per-camera uniform alongside irradianceIntensity.
		let IBL_SPEC_SCALE: f32 = 0.15;
		let reflectVec = reflect(-viewDir, worldNormal);
		let prefilteredUV = direction_to_equirect_uv(reflectVec);
		let maxMip = max(0.0, f32(textureNumLevels(prefiltered_env)) - 1.0);
		let envSpec = textureSampleLevel(prefiltered_env, environment_sampler, prefilteredUV, roughness * maxMip).rgb;
		let envBRDF = textureSample(brdf_lut, environment_sampler, vec2<f32>(NdotV, roughness)).rg;
		finalColor += envSpec * (F * envBRDF.x + envBRDF.y) * u_environment.params.y * ao * IBL_SPEC_SCALE;
	}

	// Iterate through lights affecting this pixel
	for (var i: u32 = 0u; i < lightCount; i++) {
		let lightIndexOffset = clusterLights.offset + i;
		if (lightIndexOffset >= arrayLength(&u_cluster_light_indices)) { break; }
		let lightIdx = u_cluster_light_indices[lightIndexOffset];
		if (lightIdx >= 5120u) { break; } // Safety check

		let light = u_lights.lights[lightIdx];

		var contribution = vec3<f32>(0.0);
		if (light.light_type == 0u) {
			// Ambient is an albedo-tinted flat fill. The G-buffer carries no
			// per-material ambient term, so this stays inline rather than going
			// through the shared evaluator (which only covers analytic
			// directional / point / spot lights).
			contribution = albedoLinear * light.color * light.intensity;
		} else {
			// Same Cook-Torrance GGX path the forward transparent shader uses
			// (lib/direct_lighting.wgsl) — opaque and transparent surfaces now
			// light identically. transmission = 0: opaque keeps its full diffuse
			// lobe. f0 = dielectric 0.04 baseline lerped toward albedo by metallic.
			let F0 = mix(vec3<f32>(0.04), albedoLinear, metallic);
			contribution = evaluate_direct_light_ggx(
				light, worldNormal, viewDir, worldPos, albedoLinear, F0, roughness, metallic, 0.0
			);
		}

		contribution *= calculate_shadow(worldPos, worldNormal, light);
		finalColor += contribution;
	}
	
	// Apply AO softly to reduce over-darkening from noisy AO textures.
	finalColor *= mix(1.0, ao, 0.5);

	// Emission is added AFTER lighting so emissive surfaces (sky-domes, neon,
	// magic, baked GI fallback) are not multiplied by lighting and survive
	// when base color is dark or shadows are full. Mirrors PBR_Lit_Shader's
	// `Lo + irradiance + emission` formulation.
	finalColor += emission;

	// Return raw linear HDR. CompositePass (fullscreen_quad.wgsl) does the
	// tone-mapping in one place so this pass, the skybox, and any future
	// transparency pass all share the same curve.
	return vec4<f32>(finalColor, 1.0);
}
