// G-Buffer geometry pass for deferred shading.
//
// Writes per-pixel surface data into four render targets so the
// composition pass can apply clustered lighting per pixel.
//
// Render targets (locations must match engine::rendering::webgpu::GBuffer):
//   @location(0) position : RGBA16Float - xyz = world position, w = view-space depth
//   @location(1) normal   : RGBA16Float - xyz = world normal,   w = view-space depth
//   @location(2) albedo   : RGBA8UnormSrgb - rgb = base color * diffuse, a = coverage
//   @location(3) material : RGBA8Unorm    - r = roughness, g = metallic, b = AO
//                                          - a = emission intensity (luminance proxy)
//
// Bind group layout (must match PBR_Lit_Shader.wgsl material layout exactly so
// WebGPUMaterial's cached bind group can be reused by either pass):
//   group 0 : FrameUniforms
//   group 1 : ObjectUniforms
//   group 2 : material - properties (0), sampler (1),
//                        baseColor (2), normal (3), ao (4),
//                        roughness (5), metallic (6), emission (7)

struct FrameUniforms {
	viewMatrix: mat4x4<f32>,
	projectionMatrix: mat4x4<f32>,
	viewProjectionMatrix: mat4x4<f32>,
	cameraWorldPosition: vec3<f32>,
	time: f32,
}

struct ObjectUniforms {
	modelMatrix: mat4x4<f32>,
	modelInvTranspose: mat4x4<f32>,
}

struct PBRProperties {
	diffuse: vec4<f32>,
	emission: vec4<f32>,
	transmittance: vec4<f32>,
	ambient: vec4<f32>,
	roughness: f32,
	metallic: f32,
	ior: f32,
	normalTextureScale: f32,
}

@group(0) @binding(0) var<uniform> uFrame: FrameUniforms;
@group(1) @binding(0) var<uniform> uObject: ObjectUniforms;

@group(2) @binding(0) var<uniform> uMaterial: PBRProperties;
@group(2) @binding(1) var textureSampler: sampler;
@group(2) @binding(2) var baseColorTexture: texture_2d<f32>;
@group(2) @binding(3) var normalTexture: texture_2d<f32>;
@group(2) @binding(4) var aoTexture: texture_2d<f32>;
@group(2) @binding(5) var roughnessTexture: texture_2d<f32>;
@group(2) @binding(6) var metallicTexture: texture_2d<f32>;
@group(2) @binding(7) var emissionTexture: texture_2d<f32>;

struct VertexInput {
	@location(0) position: vec3<f32>,
	@location(1) normal: vec3<f32>,
	@location(2) texCoord: vec2<f32>,
	@location(3) tangent: vec4<f32>,
}

struct VertexOutput {
	@builtin(position) clipPosition: vec4<f32>,
	@location(0) worldPos: vec3<f32>,
	@location(1) worldNormal: vec3<f32>,
	@location(2) texCoord: vec2<f32>,
	@location(3) worldTangent: vec3<f32>,
	@location(4) tangentSign: f32,
}

struct FragmentOutput {
	@location(0) position: vec4<f32>,
	@location(1) normal: vec4<f32>,
	@location(2) albedo: vec4<f32>,
	@location(3) material: vec4<f32>,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
	var output: VertexOutput;

	let worldPos = (uObject.modelMatrix * vec4<f32>(input.position, 1.0)).xyz;
	output.worldPos = worldPos;
	output.clipPosition = uFrame.viewProjectionMatrix * vec4<f32>(worldPos, 1.0);

	// Pass through raw world-space N and T - the fragment shader re-orthogonalises
	// and reconstructs B from them. Mirrors the PBR_Lit_Shader setup so the visual
	// result of normal mapping is identical across forward and deferred paths.
	output.worldNormal = normalize((uObject.modelInvTranspose * vec4<f32>(input.normal, 0.0)).xyz);
	output.worldTangent = (uObject.modelMatrix * vec4<f32>(input.tangent.xyz, 0.0)).xyz;
	// tangent.w carries the handedness sign produced by the importer.
	output.tangentSign = input.tangent.w;

	output.texCoord = input.texCoord;
	return output;
}

@fragment
fn fs_main(input: VertexOutput) -> FragmentOutput {
	let baseSample = textureSample(baseColorTexture, textureSampler, input.texCoord);
	let coverage = baseSample.a * uMaterial.diffuse.a;
	// Low alpha-test threshold so BLEND-but-actually-opaque GLTF meshes
	// (very common in Sketchfab/Blender exports) still write depth. Truly
	// blended geometry would need a forward transparency pass.
	if (coverage < 0.01) {
		discard;
	}

	let normalSample = textureSample(normalTexture, textureSampler, input.texCoord);
	let aoSample = textureSample(aoTexture, textureSampler, input.texCoord).r;
	let roughnessSample = textureSample(roughnessTexture, textureSampler, input.texCoord).r;
	let metallicSample = textureSample(metallicTexture, textureSampler, input.texCoord).r;
	let emissionSample = textureSample(emissionTexture, textureSampler, input.texCoord).rgb;

	// Tangent-space normal -> world space. Match PBR_Lit_Shader exactly:
	//   1. Re-orthogonalise T against N to recover the right basis after
	//      interpolation (Gram-Schmidt).
	//   2. Scale only the XY of the unpacked tangent-space normal - scaling
	//      a normalised vector and rotating it does NOT add or remove relief,
	//      it just changes the magnitude pre-renormalisation. Leaving Z
	//      unscaled is what actually flattens/strengthens the bump.
	//   3. Build TBN and rotate, then normalise once at the end.
	let n0 = normalize(input.worldNormal);
	let t0 = normalize(input.worldTangent - n0 * dot(n0, input.worldTangent));
	let b0 = cross(n0, t0) * input.tangentSign;
	let TBN = mat3x3<f32>(t0, b0, n0);

	let normalUnpacked = normalSample.rgb * 2.0 - 1.0;
	let scaledXY = clamp(normalUnpacked.xy * uMaterial.normalTextureScale, vec2<f32>(-2.0), vec2<f32>(2.0));
	let worldNormal = normalize(TBN * vec3<f32>(scaledXY.x, scaledXY.y, normalUnpacked.z));

	let roughness = roughnessSample * uMaterial.roughness;
	let metallic = metallicSample * uMaterial.metallic;
	let ao = aoSample;

	// View-space depth is packed into the alpha channel of position/normal targets
	// so the composition pass can do depth-based clustering without a separate sample.
	let viewPos = uFrame.viewMatrix * vec4<f32>(input.worldPos, 1.0);
	let viewDepth = -viewPos.z;

	let baseColor = baseSample.rgb * uMaterial.diffuse.rgb;
	let emission = emissionSample * uMaterial.emission.rgb;
	// Pack emission luminance into the unused material.a channel; composition
	// multiplies it back out as additive light per pixel.
	let emissionLuma = dot(emission, vec3<f32>(0.2126, 0.7152, 0.0722));

	var output: FragmentOutput;
	output.position = vec4<f32>(input.worldPos, viewDepth);
	output.normal = vec4<f32>(worldNormal, viewDepth);
	output.albedo = vec4<f32>(baseColor, coverage);
	output.material = vec4<f32>(roughness, metallic, ao, clamp(emissionLuma, 0.0, 1.0));
	return output;
}
