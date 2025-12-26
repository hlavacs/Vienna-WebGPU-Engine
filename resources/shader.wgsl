struct VertexInput {
	@location(0) position: vec3f,
	@location(1) normal: vec3f,
	@location(2) color: vec3f,
	@location(3) uv: vec2f,
	@location(4) tangent: vec3f,
	@location(5) bitangent: vec3f,
}

;

struct VertexOutput {
	@builtin(position) position: vec4f,
	@location(0) color: vec3f,
	@location(1) normal: vec3f,
	@location(2) uv: vec2f,
	@location(3) viewDirection: vec3f,
	@location(4) tangent: vec3f,
	@location(5) bitangent: vec3f,
	@location(6) worldPosition: vec4f,
}

;

struct FrameUniforms {
	viewMatrix: mat4x4f,
	projectionMatrix: mat4x4f,
	viewProjectionMatrix: mat4x4f,
	cameraWorldPosition: vec3f,
	time: f32,
}

;

struct Light {
	transform: mat4x4f,
	color: vec3f,
	intensity: f32,
	light_type: u32,
	// 0 = ambient, 1 = directional, 2 = point, 3 = spot
	spot_angle: f32,
	spot_softness: f32,
	_pad: f32,
}

;

struct LightsBuffer {
	count: u32,
	_pad1: f32,
	// Padding to align to 16 bytes
	_pad2: f32,
	// Padding to align to 16 bytes
	_pad3: f32,
	// Padding to align to 16 bytes
	lights: array<Light>,
}

;

struct ObjectUniforms {
	modelMatrix: mat4x4f,
	normalMatrix: mat4x4f,
}

;

struct MaterialUniforms {
	// xyz = baseColor, w = opacity
	diffuse: vec4f,
	// xyz = emissiveColor, w = emissionIntensity
	emission: vec4f,
	// xyz = transmittanceColor, w = transmittanceIntensity
	transmittance: vec4f,
	// xyz = ambientColor, w = ambientIntensity
	ambient: vec4f,

	roughness: f32,
	metallic: f32,
	ior: f32,
	_pad0: f32
}

;

@group(0) @binding(0)
var<uniform> uFrame: FrameUniforms;

@group(1) @binding(0)
var<storage, read> uLights: LightsBuffer;

@group(2) @binding(0)
var<uniform> uObject: ObjectUniforms;

@group(3) @binding(0)
var<uniform> uMaterial: MaterialUniforms;
@group(3) @binding(1)
var textureSampler: sampler;
@group(3) @binding(2)
var baseColorTexture: texture_2d<f32>;
@group(3) @binding(3)
var normalTexture: texture_2d<f32>;
@group(3) @binding(4)
var aoTexture: texture_2d<f32>;
@group(3) @binding(5)
var roughnessTexture: texture_2d<f32>;
@group(3) @binding(6)
var metallicTexture: texture_2d<f32>;
@group(3) @binding(7)
var emissionTexture: texture_2d<f32>;

const PI: f32 = 3.141592653589793;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	let worldPosition = uObject.modelMatrix * vec4<f32>(in.position, 1.0);
	out.worldPosition = worldPosition;
	out.position = uFrame.viewProjectionMatrix * worldPosition;
	out.normal = normalize((uObject.normalMatrix * vec4f(in.normal, 0.0)).xyz);
	out.tangent = normalize((uObject.normalMatrix * vec4f(in.tangent, 0.0)).xyz);
	out.bitangent = normalize((uObject.normalMatrix * vec4f(in.bitangent, 0.0)).xyz);
	out.color = in.color;
	out.uv = in.uv;
	out.viewDirection = uFrame.cameraWorldPosition - worldPosition.xyz;
	return out;
}

// ------------------------------------------------------------
// Utility
// ------------------------------------------------------------
fn saturate(v: f32) -> f32 {
	return clamp(v, 0.0, 1.0);
}

fn fresnelSchlick(cosTheta: f32, F0: vec3f) -> vec3f {
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

fn distributionGGX(N: vec3f, H: vec3f, roughness: f32) -> f32 {
	let a = roughness * roughness;
	let a2 = a * a;
	let NdotH = max(dot(N, H), 0.0);
	let NdotH2 = NdotH * NdotH;

	let denom = (NdotH2 * (a2 - 1.0) + 1.0);
	return a2 / (PI * denom * denom);
}

fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
	let r = roughness + 1.0;
	let k = (r * r) / 8.0;
	return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N: vec3f, V: vec3f, L: vec3f, roughness: f32) -> f32 {
	let NdotV = max(dot(N, V), 0.0);
	let NdotL = max(dot(N, L), 0.0);
	let ggx1 = geometrySchlickGGX(NdotV, roughness);
	let ggx2 = geometrySchlickGGX(NdotL, roughness);
	return ggx1 * ggx2;
}

fn getDirectionFromTransform(transform: mat4x4f) -> vec3f {
	return normalize(- transform[2].xyz);
}

fn getPositionFromTransform(transform: mat4x4f) -> vec3f {
	return transform[3].xyz;
}

// ------------------------------------------------------------
// Fragment
// ------------------------------------------------------------
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
	// ---- Normal mapping ----
	let N = normalize(in.normal);
	let T = normalize(in.tangent - N * dot(N, in.tangent));
	let B = cross(N, T);

	let TBN = mat3x3f(T, B, N);
	let normal = normalize(TBN * (textureSample(normalTexture, textureSampler, in.uv).rgb * 2.0 - 1.0));

	let V = normalize(in.viewDirection);

	// ---- Material textures ----
	let baseColor = textureSample(baseColorTexture, textureSampler, in.uv).rgb * uMaterial.diffuse.rgb;
	let roughness = clamp(textureSample(roughnessTexture, textureSampler, in.uv).r * uMaterial.roughness, 0.001, 1.0);
	let metallic = textureSample(metallicTexture, textureSampler, in.uv).r * uMaterial.metallic;
	let aoTex = textureSample(aoTexture, textureSampler, in.uv).r;
	let ao = saturate(aoTex);
	let emission = textureSample(emissionTexture, textureSampler, in.uv).rgb * uMaterial.emission.rgb * uMaterial.emission.w;

	// ---- Fresnel base reflectance ----
	let ior = max(uMaterial.ior, 1.0);
	let F0_dielectric = pow((ior - 1.0) / (ior + 1.0), 2.0);
	let F0 = mix(vec3f(F0_dielectric), baseColor, metallic);

	var Lo = vec3f(0.0);
	let worldPos = in.worldPosition.xyz;

	for (var i: u32 = 0u; i < uLights.count; i = i + 1u) {
		let light = uLights.lights[i];
		if (light.light_type == 0u) {
			// Ambient only
			Lo += baseColor * uMaterial.ambient.rgb * uMaterial.ambient.w * light.color * light.intensity * ao;
			continue;
		}
		var L = vec3f(0.0);
		var attenuation = 1.0;

		if (light.light_type == 1u) {
			// Directional light
			L = getDirectionFromTransform(light.transform);
		}
		else if (light.light_type == 2u) {
			// Point light
			let lightPos = getPositionFromTransform(light.transform);
			let toLight = lightPos - worldPos;
			let dist = length(toLight);
			L = normalize(toLight);
			attenuation = 1.0 / max(dist * dist, 0.01);
		}
		else if (light.light_type == 3u) {
			// Spotlight
			let lightPos = getPositionFromTransform(light.transform);
			let toLight = lightPos - worldPos;
			let dist = length(toLight);
			L = normalize(toLight);

			// Spotlight cone
			let spotDir = getDirectionFromTransform(light.transform);
			let cosTheta = dot(L, spotDir);
			let innerRatio = 1.0 - max(0.01, light.spot_softness);
			let cosOuter = cos(light.spot_angle);
			let cosInner = cos(light.spot_angle * innerRatio);
			let spotEffect = smoothstep(cosOuter, cosInner, cosTheta);
			attenuation = select(0.0, spotEffect / (1.0 + 0.1 * dist + 0.01 * dist * dist), cosTheta > cosOuter);
		}

		let H = normalize(V + L);
		let NdotL = max(dot(normal, L), 0.0);
		let NdotV = max(dot(normal, V), 0.0);
		if (NdotL <= 0.0) {
			continue;
		}

		let D = distributionGGX(normal, H, roughness);
		let G = geometrySmith(normal, V, L, roughness);
		let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

		let numerator = D * G * F;
		let denominator = max(4.0 * NdotV * NdotL, 0.001);
		let specular = numerator / denominator;

		let kS = F;
		let kD = (vec3f(1.0) - kS) * (1.0 - metallic);

		let radiance = light.color * light.intensity * attenuation;
		Lo += (kD * baseColor / PI + specular) * radiance * NdotL;
	}

	// ---- Emission ----
	let color = Lo + emission;

	return vec4f(color, uMaterial.diffuse.w);
}



