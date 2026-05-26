// Deferred Composition Shader
// Reads G-buffer data and performs PBR lighting using clustered lights

// G-Buffer samplers
@group(0) @binding(0) var gBufferPositionTexture: texture_2d<f32>;
@group(0) @binding(1) var gBufferNormalTexture: texture_2d<f32>;
@group(0) @binding(2) var gBufferAlbedoTexture: texture_2d<f32>;
@group(0) @binding(3) var gBufferMaterialTexture: texture_2d<f32>;
// RGBA16Float in C++ (alpha unused). Emission is added post-lighting so
// emissive surfaces (sky-domes, neon, eyes, magic) survive the deferred
// "albedo * lighting" multiplication that would otherwise zero them out when
// base color is dark.
@group(0) @binding(4) var gBufferEmissionTexture: texture_2d<f32>;

// Frame uniforms (camera, time)
@group(1) @binding(0) var<uniform> uFrame: FrameUniforms;

// Light data and cluster grid
@group(2) @binding(0) var<storage, read> uLights: LightBuffer;
// Shadow maps and shadow metadata
@group(3) @binding(0) var shadowSampler: sampler_comparison;
@group(3) @binding(1) var shadowMaps2D: texture_depth_2d_array;
@group(3) @binding(2) var shadowMapsCube: texture_depth_cube_array;
@group(3) @binding(3) var<storage, read> uShadows: array<ShadowUniform>;

@group(4) @binding(0) var<storage, read> uClusterGrid: array<ClusterLightList>;
@group(4) @binding(1) var<storage, read> uClusterLightIndices: array<u32>;  // Flat buffer of light indices

// Environment irradiance (matches the PBR ENVIRONMENT layout exactly so the
// same per-camera bind group built by Renderer::updateEnvironmentBindGroup
// can feed forward and deferred paths interchangeably).
// params.x = irradiance enabled (0/1), .y = intensity, .z = skybox enabled, .w = reserved
struct EnvironmentUniforms {
	params: vec4<f32>,
}
@group(5) @binding(0) var<uniform> uEnvironment: EnvironmentUniforms;
@group(5) @binding(1) var environmentSampler: sampler;
@group(5) @binding(2) var environmentTexture: texture_2d<f32>;

// Structures (should match C++ definitions)
struct FrameUniforms {
	viewMatrix: mat4x4<f32>,
	projectionMatrix: mat4x4<f32>,
	viewProjectionMatrix: mat4x4<f32>,
	cameraWorldPosition: vec3<f32>,
	time: f32,
}

struct LightStruct {
	transform: mat4x4<f32>,
	color: vec3<f32>,
	intensity: f32,
	lightType: u32,
	spotAngle: f32,
	spotSoftness: f32,
	range: f32,
	shadowIndex: u32,
	shadowCount: u32,
	_pad1: f32,
	_pad2: f32,
}

struct LightBuffer {
	count: u32,
	_pad: array<u32, 3>,
	lights: array<LightStruct>,
}

struct ShadowUniform {
	viewProj: mat4x4<f32>,
	lightPos: vec3<f32>,
	near: f32,
	far: f32,
	bias: f32,
	normalBias: f32,
	texelSize: f32,
	pcfKernel: u32,
	shadowType: u32,
	textureIndex: u32,
	cascadeSplit: f32,
}

struct ClusterLightList {
	offset: u32,   // Offset into flat light index buffer
	count: u32,    // Number of lights in this cluster
}

// PBR properties structure
struct PBRProperties {
	roughness: f32,
	metallic: f32,
	ambientOcclusion: f32,
	_pad: f32,
}

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
	let dir = normalize(direction);
	let u = atan2(dir.z, dir.x) * INV_2PI + 0.5;
	let v = acos(clamp(dir.y, -1.0, 1.0)) * INV_PI;
	let raw = textureSample(environmentTexture, environmentSampler, vec2<f32>(u, v)).rgb;
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
// world units (positionData.w from g_buffer), not NDC depth - log-Z over view
// space distributes lights across the Z slabs; NDC depth concentrates 95% of
// fragments in the last slab and collapses the grid to 1D.
const CLUSTER_Z_NEAR: f32 = 0.1;
const CLUSTER_Z_FAR:  f32 = 1000.0;

fn getClusterIndex(uv: vec2<f32>, viewDepth: f32) -> u32 {
	let gridDimX = 24u;
	let gridDimY = 14u;
	let gridDimZ = 32u;

	let x = u32(clamp(uv.x, 0.0, 0.999999) * f32(gridDimX));
	let y = u32(clamp(uv.y, 0.0, 0.999999) * f32(gridDimY));

	let clamped = clamp(viewDepth, CLUSTER_Z_NEAR, CLUSTER_Z_FAR);
	let normalized = log(clamped / CLUSTER_Z_NEAR) / log(CLUSTER_Z_FAR / CLUSTER_Z_NEAR);
	let z = u32(clamp(normalized, 0.0, 0.999999) * f32(gridDimZ));

	return z * (gridDimX * gridDimY) + y * gridDimX + x;
}

fn saturate(v: f32) -> f32 {
	return clamp(v, 0.0, 1.0);
}

fn getDirectionFromTransform(transform: mat4x4<f32>) -> vec3<f32> {
	return normalize(-transform[2].xyz);
}

fn getPositionFromTransform(transform: mat4x4<f32>) -> vec3<f32> {
	return transform[3].xyz;
}

fn selectCascade(viewDepth: f32, light: LightStruct) -> u32 {
	for (var i: u32 = 0u; i < light.shadowCount; i = i + 1u) {
		let shadowIdx = light.shadowIndex + i;
		let shadow = uShadows[shadowIdx];
		if (viewDepth <= shadow.cascadeSplit) {
			return i;
		}
	}
	return light.shadowCount - 1u;
}

fn calculateShadow(worldPos: vec3<f32>, normal: vec3<f32>, light: LightStruct) -> f32 {
	if (light.shadowCount == 0u) {
		return 1.0;
	}

	if (light.lightType == 1u || light.lightType == 3u) {
		var shadowIndex = light.shadowIndex;

		if (light.lightType == 1u && light.shadowCount > 1u) {
			let viewPos = uFrame.viewMatrix * vec4<f32>(worldPos, 1.0);
			let viewDepth = -viewPos.z;
			let cascadeIdx = selectCascade(viewDepth, light);
			shadowIndex = light.shadowIndex + cascadeIdx;
		}

		let shadow = uShadows[shadowIndex];
		let lightSpacePos = shadow.viewProj * vec4<f32>(worldPos, 1.0);
		if (lightSpacePos.w <= 0.00001) {
			return 1.0;
		}

		let shadowProj = lightSpacePos.xyz / lightSpacePos.w;
		let shadowUv = shadowProj.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
		let shadowDepth = shadowProj.z;
		if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
			shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
			shadowDepth <= 0.0 || shadowDepth >= 1.0) {
			return 1.0;
		}

		let lightDir = normalize(getDirectionFromTransform(light.transform));
		let ndotl = max(dot(normal, lightDir), 0.0);
		let slopeBias = shadow.normalBias * (1.0 - ndotl);
		var finalBias: f32;

		if (light.lightType == 1u) {
			let texelBias = shadow.bias * shadow.texelSize;
			finalBias = max(texelBias, 0.001) + max(slopeBias * shadow.texelSize, 0.001);
		} else {
			let depthScale = abs(shadowDepth);
			let distanceScale = smoothstep(shadow.near, shadow.far, depthScale);
			finalBias = (shadow.bias + slopeBias) * distanceScale;
		}

		finalBias = clamp(finalBias, 0.000001, 0.02);
		let currentDepth = shadowDepth - finalBias;

		var pcfScale = 1.0;
		var shadowStrength = 1.0;
		if (light.lightType == 1u) {
			let sunDotUp = saturate(dot(lightDir, vec3<f32>(0.0, 1.0, 0.0)));
			shadowStrength = smoothstep(0.0, 0.4, sunDotUp);
			pcfScale = mix(0.5, 1.0, shadowStrength);
		}

		var visibility = 0.0;
		let kernel = i32(shadow.pcfKernel);
		var samples = 0.0;
		for (var x = -kernel; x <= kernel; x = x + 1) {
			for (var y = -kernel; y <= kernel; y = y + 1) {
				let offset = vec2<f32>(f32(x), f32(y)) * shadow.texelSize * pcfScale;
				let uv = shadowUv + offset;
				visibility += textureSampleCompare(shadowMaps2D, shadowSampler, uv, shadow.textureIndex, currentDepth);
				samples += 1.0;
			}
		}

		if (light.lightType == 1u) {
			return mix(1.0, visibility / samples, shadowStrength);
		}

		return visibility / samples;
	} else if (light.lightType == 2u) {
		let shadow = uShadows[light.shadowIndex];
		let toFrag = (worldPos - shadow.lightPos) * vec3<f32>(-1.0, 1.0, 1.0);
		let linearDepth = length(toFrag);
		if (linearDepth >= shadow.far) {
			return 1.0;
		}

		let sampleDir = normalize(toFrag);
		let ndotl = max(dot(normal, sampleDir), 0.0);
		let slopeBias = shadow.normalBias * (1.0 - ndotl);
		let finalBias = shadow.bias + slopeBias;
		let currentDepth = clamp((linearDepth - finalBias) / shadow.far, 0.0, 1.0);

		var visibility = 0.0;
		var samples = 0.0;
		let kernel = i32(shadow.pcfKernel);
		let radius = shadow.texelSize * (linearDepth / shadow.far);
		let w = sampleDir;
		var up = vec3<f32>(0.0, 1.0, 0.0);
		if (abs(w.y) > 0.999) { up = vec3<f32>(1.0, 0.0, 0.0); }
		let u = normalize(cross(up, w));
		let v = cross(w, u);

		for (var x = -kernel; x <= kernel; x = x + 1) {
			for (var y = -kernel; y <= kernel; y = y + 1) {
				for (var z = -kernel; z <= kernel; z = z + 1) {
					let offset = u * f32(x) + v * f32(y) + w * f32(z);
					let sampleDirOffset = normalize(toFrag + offset * radius);
					visibility += textureSampleCompare(shadowMapsCube, shadowSampler, sampleDirOffset, shadow.textureIndex, currentDepth);
					samples += 1.0;
				}
			}
		}

		return visibility / samples;
	}

	return 1.0;
}

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
	let viewDir = normalize(uFrame.cameraWorldPosition - worldPos);

	// Trust the cluster compute - count=0 means no direct light affects this
	// pixel and we just emit IBL + emission. The old "scan all 512 lights as
	// a fallback when count=0" path defeated the entire optimization for
	// every pixel outside the lit volume.
	let clusterIdx = getClusterIndex(uvCluster, viewDepth);
	let clusterLights = uClusterGrid[clusterIdx];
	let lightCount = min(clusterLights.count, 256u);

	// Accumulate lighting. Start with image-based ambient: sample the
	// environment map along the world normal as a cheap diffuse-irradiance
	// approximation. uEnvironment.params.x toggles it, .y is intensity.
	// Doing this BEFORE the direct light loop means scenes with zero
	// dynamic lights still pick up environment color instead of going black.
	// Matches PBR_Lit_Shader: irradiance * baseColor * (1 - metallic) * ao
	// so metals don't get diffuse ambient and cavities stay dark.
	var finalColor = vec3<f32>(0.0);
	if (uEnvironment.params.x > 0.5) {
		let envIrradiance = sampleEnvironmentAtDirection(worldNormal);
		finalColor += albedoLinear * envIrradiance * uEnvironment.params.y * (1.0 - metallic) * ao;
	}

	// Iterate through lights affecting this pixel
	for (var i: u32 = 0u; i < lightCount; i++) {
		let lightIndexOffset = clusterLights.offset + i;
		if (lightIndexOffset >= arrayLength(&uClusterLightIndices)) { break; }
		let lightIdx = uClusterLightIndices[lightIndexOffset];
		if (lightIdx >= 5120u) { break; } // Safety check
		
		let light = uLights.lights[lightIdx];
		let lightType = light.lightType;
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
				let innerRatio = 1.0 - max(0.01, light.spotSoftness);
				let cosOuter = cos(light.spotAngle);
				let cosInner = cos(light.spotAngle * innerRatio);
				let spotEffect = smoothstep(cosOuter, cosInner, cosTheta);
				let attenuation = (1.0 / max(distance * distance, 0.001)) * spotEffect;
				let ndotL = max(0.0, dot(worldNormal, lightDir));
				let halfVec = normalize(lightDir + viewDir);
				let specPower = mix(64.0, 4.0, roughness);
				let specular = pow(max(dot(worldNormal, halfVec), 0.0), specPower) * (1.0 - metallic);
				contribution = lightColor * lightIntensity * attenuation * (ndotL / PI + 0.15 * specular);
			}
		}
		
		contribution *= calculateShadow(worldPos, worldNormal, light);
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
