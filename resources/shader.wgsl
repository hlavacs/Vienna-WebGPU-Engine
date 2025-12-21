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
	// xyz = specularColor, w = specularStrength
	specular: vec4f,
	// xyz = emissiveColor, w = emissionIntensity
	emission: vec4f,
	// xyz = transmittanceColor, w = transmittanceIntensity
	transmittance: vec4f,

	shininess: f32,
	roughness: f32,
	metallic: f32,
	ior: f32,
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
const AMBIENT_INTENSITY: f32 = 0.03;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	let worldPosition = uObject.modelMatrix * vec4<f32>(in.position, 1.0);
	out.position = uFrame.viewProjectionMatrix * worldPosition;
	out.tangent = (uObject.modelMatrix * vec4f(in.tangent, 0.0)).xyz;
	out.bitangent = (uObject.modelMatrix * vec4f(in.bitangent, 0.0)).xyz;
	out.normal = (uObject.modelMatrix * vec4f(in.normal, 0.0)).xyz;
	out.color = in.color;
	out.uv = in.uv;
	out.viewDirection = uFrame.cameraWorldPosition - worldPosition.xyz;
	return out;
}

fn getDirectionFromTransform(transform: mat4x4f) -> vec3f {
	return normalize(- transform[2].xyz);
}

fn getPositionFromTransform(transform: mat4x4f) -> vec3f {
	return transform[3].xyz;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
	// Sample normal
	let normalMapStrength = 1.0;
	let encodedN = textureSample(normalTexture, textureSampler, in.uv).rgb;
	let localN = encodedN * 2.0 - 1.0;
	// The TBN matrix converts directions from the local space to the world space
	let localToWorld = mat3x3f(normalize(in.tangent), normalize(in.bitangent), normalize(in.normal),);
	let worldN = localToWorld * localN;
	let N = normalize(mix(in.normal, worldN, normalMapStrength));

	let V = normalize(in.viewDirection);

	// Sample texture
	let baseColor = textureSample(baseColorTexture, textureSampler, in.uv).rgb;

	// Compute shading
	var color = vec3f(0.0);

	// Calculate world position from view direction and camera position
	let worldPos = uFrame.cameraWorldPosition - in.viewDirection;

	// Get material properties from the buffer
	let kd = 1.0;
	// Diffuse coefficient (using material diffuse color below)
	let ks = length(uMaterial.specular.rgb) / 1.732;
	// Specular coefficient (normalize from vec3 to scalar)
	let hardness = uMaterial.shininess;

	for (var i: u32 = 0u; i < uLights.count; i = i + 1u) {
		let light = uLights.lights[i];

		if (light.light_type == 0u) {
			// Ambient light: just add its color * intensity
			color += baseColor * uMaterial.diffuse.rgb * light.color * light.intensity;
			continue;
		}

		var L = vec3f(0.0);
		var attenuation = 1.0;

		if (light.light_type == 1u) {
			// Directional
			L = getDirectionFromTransform(light.transform);
		}
		else if (light.light_type == 2u) {
			// Point
			let lightPos = getPositionFromTransform(light.transform);
			L = normalize(lightPos - worldPos);
			let dist = length(lightPos - worldPos);
			attenuation = 1.0 / (dist * dist);
		}
		else if (light.light_type == 3u) {
			// Spot light calculation
			let lightPos = getPositionFromTransform(light.transform);
			L = normalize(lightPos - worldPos);
			let dist = length(lightPos - worldPos);

			// Get spotlight direction from transform matrix
			let spotDir = getDirectionFromTransform(light.transform);

			// Calculate cosine of angle between light vector and spotlight direction
			let cosTheta = dot(L, spotDir);

			// Calculate spotlight effect with configurable soft falloff
			let softnessFactor = max(0.01, light.spot_softness);
			// Ensure we don't divide by zero

			// Calculate the inner cone angle based on softness
			// Higher softness = smaller inner cone = smoother transition
			let innerRatio = 1.0 - softnessFactor;
			let cosOuter = cos(light.spot_angle);
			let cosInner = cos(light.spot_angle * innerRatio);

			// Smooth spotlight falloff between inner and outer cones
			let spotEffect = smoothstep(cosOuter, cosInner, cosTheta);

			// Apply spotlight effect with distance attenuation
			attenuation = spotEffect * 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);

			// Set attenuation to zero if outside the cone
			attenuation = select(0.0, attenuation, cosTheta > cosOuter);
		}

		let diffuse = max(0.0, dot(L, N)) * light.color.rgb * light.intensity * attenuation * kd;
		let R = reflect(- L, N);
		let specular = pow(max(0.0, dot(R, V)), hardness) * ks * light.intensity * attenuation;

		// Combine texture color with material diffuse color and apply lighting
		color += baseColor * uMaterial.diffuse.rgb * diffuse + uMaterial.specular.rgb * specular;
	}

	// Gamma-correction
	let corrected_color = pow(color, vec3f(2.2));
	return vec4f(corrected_color, uMaterial.diffuse.w);
}
