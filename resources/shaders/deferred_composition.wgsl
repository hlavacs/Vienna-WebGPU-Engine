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

const PI: f32 = 3.141592653589793;
const INV_PI: f32 = 0.31830988618;
const INV_2PI: f32 = 0.15915494309;

// Sample the equirect environment map at a world-space direction as a cheap
// diffuse-irradiance approximation. Without a pre-convolved irradiance map
// we just clamp the raw HDR sample to a low ceiling: high enough to provide
// useful ambient colour but low enough that geometry with its own baked
// detail (like a textured GLTF sky-dome) doesn't get washed out by the
// per-channel multiply in the lighting equation.
fn sampleEnvironmentAtDirection(direction: vec3<f32>) -> vec3<f32> {
	let uv  = direction_to_equirect_uv(direction);
	let raw = textureSample(environment_texture, environment_sampler, uv).rgb;
	return min(raw, vec3<f32>(0.3));
}

// Specular IBL: sample the env equirect at the reflection vector and select
// a mip level based on roughness. Cheap split-sum approximation — no
// pre-filtered cubemap, just rely on the existing mip chain to do the
// pre-filtering for us. Mirror-smooth surfaces (roughness=0) hit mip 0;
// fully-rough surfaces hit the smallest mip and look matte.
//
// HDR sample clamped to the same ceiling as the diffuse path (vec3(0.3)).
// Without prefiltering, leaving this unclamped lets the sun (which can
// spike to 10-50× in the HDR equirect) bleed through Fresnel into every
// pixel — even at F0=0.04 dielectric, F * 50 = 2.0 of unwanted brightness.
// Clamping kills the HDR highlight look but keeps the engine usable;
// proper split-sum prefiltering will restore real specular highlights.
fn sampleEnvironmentReflection(direction: vec3<f32>, roughness: f32) -> vec3<f32> {
	let uv     = direction_to_equirect_uv(direction);
	let maxMip = max(0.0, f32(textureNumLevels(environment_texture)) - 1.0);
	let mip    = clamp(roughness, 0.0, 1.0) * maxMip;
	let raw    = textureSampleLevel(environment_texture, environment_sampler, uv, mip).rgb;
	return min(raw, vec3<f32>(0.3));
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

// Cluster math MUST match light_clustering.wgsl. depth is VIEW-SPACE depth in
// world units (positionData.w from g_buffer), not NDC depth — log-Z over view
// space distributes lights across the Z slabs. CLUSTER_GRID_DIM_*,
// CLUSTER_Z_NEAR, CLUSTER_Z_FAR come from the codegen include below so the
// values live in one place (C++ ClusterManager constants).
#include "engine://core/constants_cluster.wgsl"

fn getClusterIndex(uv: vec2<f32>, viewDepth: f32) -> u32 {
	let gridDimX = CLUSTER_GRID_DIM_X;
	let gridDimY = CLUSTER_GRID_DIM_Y;
	let gridDimZ = CLUSTER_GRID_DIM_Z;

	let x = u32(clamp(uv.x, 0.0, 0.999999) * f32(gridDimX));
	let y = u32(clamp(uv.y, 0.0, 0.999999) * f32(gridDimY));

	let clamped = clamp(viewDepth, CLUSTER_Z_NEAR, CLUSTER_Z_FAR);
	let normalized = log(clamped / CLUSTER_Z_NEAR) / log(CLUSTER_Z_FAR / CLUSTER_Z_NEAR);
	let z = u32(clamp(normalized, 0.0, 0.999999) * f32(gridDimZ));

	return z * (gridDimX * gridDimY) + y * gridDimX + x;
}

// Shared shadow + lighting math — single source for both PBR forward and
// deferred composition.
#include "engine://lib/lighting.wgsl"
#include "engine://lib/shadow.wgsl"

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

	// Accumulate lighting. Start with image-based ambient: sample the
	// environment map along the world normal as a cheap diffuse-irradiance
	// approximation. u_environment.params.x toggles it, .y is intensity.
	// Doing this BEFORE the direct light loop means scenes with zero
	// dynamic lights still pick up environment color instead of going black.
	// Matches PBR_Lit_Shader: irradiance * baseColor * (1 - metallic) * ao
	// so metals don't get diffuse ambient and cavities stay dark.
	//
	// NOTE: specular IBL is intentionally NOT applied here. The cheap
	// "sample env at reflect vector" approach blooms grazing-angle fresnel
	// into every pixel without proper split-sum prefiltering — even clamped,
	// it changes the look of every material. The helper
	// sampleEnvironmentReflection() stays in this file for the future
	// pre-filtered cubemap pass to plug into; today's path is diffuse-only.
	var finalColor = vec3<f32>(0.0);
	if (u_environment.params.x > 0.5) {
		let envIrradiance = sampleEnvironmentAtDirection(worldNormal);
		finalColor += albedoLinear * envIrradiance * u_environment.params.y * (1.0 - metallic) * ao;
	}

	// Iterate through lights affecting this pixel
	for (var i: u32 = 0u; i < lightCount; i++) {
		let lightIndexOffset = clusterLights.offset + i;
		if (lightIndexOffset >= arrayLength(&u_cluster_light_indices)) { break; }
		let lightIdx = u_cluster_light_indices[lightIndexOffset];
		if (lightIdx >= 5120u) { break; } // Safety check
		
		let light = u_lights.lights[lightIdx];
		let lightType = light.light_type;
		let lightColor = light.color;
		let lightIntensity = light.intensity;
		
		var contribution = vec3<f32>(0.0);
		
		if (lightType == 0u) {
			// Ambient light
			contribution = lightColor * lightIntensity;
		} else if (lightType == 1u) {
			// Directional light
			let lightDir = normalize(-light.transform[2].xyz);
			let ndotL = max(0.0, dot(worldNormal, lightDir));
			let halfVec = normalize(lightDir + viewDir);
			let specPower = mix(64.0, 4.0, roughness);
			let specular = pow(max(dot(worldNormal, halfVec), 0.0), specPower) * (1.0 - metallic);
			contribution = lightColor * lightIntensity * (ndotL / PI + 0.15 * specular);
		} else if (lightType == 2u) {
			// Point light
			let lightPos = light.transform[3].xyz;
			let lightRadius = light.range;
			let toLight = lightPos - worldPos;
			let distance = length(toLight);
			
			if (distance < lightRadius) {
				let lightDir = normalize(toLight);
				let attenuation = 1.0 / max(distance * distance, 0.001);
				let ndotL = max(0.0, dot(worldNormal, lightDir));
				let halfVec = normalize(lightDir + viewDir);
				let specPower = mix(64.0, 4.0, roughness);
				let specular = pow(max(dot(worldNormal, halfVec), 0.0), specPower) * (1.0 - metallic);
				contribution = lightColor * lightIntensity * attenuation * (ndotL / PI + 0.15 * specular);
			}
		} else {
			// Spot light
			let lightPos = light.transform[3].xyz;
			let toLight = lightPos - worldPos;
			let distance = length(toLight);
			if (distance < light.range) {
				let lightDir = normalize(toLight);
				let spotDir = normalize(-light.transform[2].xyz);
				let cosTheta = dot(lightDir, spotDir);
				let innerRatio = 1.0 - max(0.01, light.spot_softness);
				let cosOuter = cos(light.spot_angle);
				let cosInner = cos(light.spot_angle * innerRatio);
				let spotEffect = smoothstep(cosOuter, cosInner, cosTheta);
				let attenuation = (1.0 / max(distance * distance, 0.001)) * spotEffect;
				let ndotL = max(0.0, dot(worldNormal, lightDir));
				let halfVec = normalize(lightDir + viewDir);
				let specPower = mix(64.0, 4.0, roughness);
				let specular = pow(max(dot(worldNormal, halfVec), 0.0), specPower) * (1.0 - metallic);
				contribution = lightColor * lightIntensity * attenuation * (ndotL / PI + 0.15 * specular);
			}
		}
		
		contribution *= calculate_shadow(worldPos, worldNormal, light);
		finalColor += albedoLinear * contribution;
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
