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
	@location(3) viewDirection: vec3<f32>,
	@location(4) tangent: vec3f,
	@location(5) bitangent: vec3f,
}

;

/**
 * A structure holding the value of our uniforms
 */

struct MyUniforms {
	projectionMatrix: mat4x4f,
	viewMatrix: mat4x4f,
	modelMatrix: mat4x4f,
	color: vec4f,
	cameraWorldPosition: vec3f,
	time: f32,
}

;

struct Light {
	color: vec3f,
	// 0 = directional, 1 = point, 2 = spot
	light_type: u32,

	position: vec3f,
	spot_angle: f32,

	rotation: vec3f,
	// Euler angles in degrees (for directional/spot)
	intensity: f32
}

;

struct LightsBuffer {
	count: u32,
	kd: f32,
	ks: f32,
	hardness: f32,
	lights: array<Light>,
}

;

@group(0) @binding(0)
var<uniform> uMyUniforms: MyUniforms;
@group(1) @binding(0)
var baseColorTexture: texture_2d<f32>;
@group(1) @binding(1)
var normalTexture: texture_2d<f32>;
@group(1) @binding(2)
var textureSampler: sampler;
@group(2) @binding(0)
var<storage, read> uLights: LightsBuffer;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	let worldPosition = uMyUniforms.modelMatrix * vec4<f32>(in.position, 1.0);
	out.position = uMyUniforms.projectionMatrix * uMyUniforms.viewMatrix * worldPosition;
	out.tangent = (uMyUniforms.modelMatrix * vec4f(in.tangent, 0.0)).xyz;
	out.bitangent = (uMyUniforms.modelMatrix * vec4f(in.bitangent, 0.0)).xyz;
	out.normal = (uMyUniforms.modelMatrix * vec4f(in.normal, 0.0)).xyz;
	out.color = in.color;
	out.uv = in.uv;
	out.viewDirection = uMyUniforms.cameraWorldPosition - worldPosition.xyz;
	return out;
}

// Helper: Convert Euler angles (degrees) to direction vector (ZYX order)
fn eulerToDirection(euler: vec3f) -> vec3f {
	let rad = euler * 3.14159265 / 180.0;
	let cx = cos(rad.x);
	let sx = sin(rad.x);
	let cy = cos(rad.y);
	let sy = sin(rad.y);
	let cz = cos(rad.z);
	let sz = sin(rad.z);
	// Forward vector for ZYX (yaw-pitch-roll):
	let x = cy * cz;
	let y = sx;
	let z = - sy * cz;
	return normalize(vec3f(x, y, z));
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
	let worldPos = uMyUniforms.cameraWorldPosition - in.viewDirection;

	// Get material properties from the buffer
	let kd = uLights.kd;
	let ks = uLights.ks;
	let hardness = uLights.hardness;

	for (var i: u32 = 0u; i < uLights.count; i = i + 1u) {
		let light = uLights.lights[i];

		var L = vec3f(0.0);
		var attenuation = 1.0;

		if (light.light_type == 0u) {
			// Directional
			L = eulerToDirection(light.rotation);
		}
		else if (light.light_type == 1u) {
			// Point
			L = normalize(light.position - worldPos);
			let dist = length(light.position - worldPos);
			attenuation = 1.0 / (dist * dist);
		}
		else if (light.light_type == 2u) {
			// Spot
			L = normalize(light.position - worldPos);
			let dist = length(light.position - worldPos);
			let spotDir = eulerToDirection(light.rotation);
			let theta = dot(L, spotDir);
			attenuation = select(0.0, 1.0 / (dist * dist), theta > cos(light.spot_angle));
		}

		let diffuse = max(0.0, dot(L, N)) * light.color.rgb * light.intensity * attenuation * kd;
		let R = reflect(- L, N);
		let specular = pow(max(0.0, dot(R, V)), hardness) * ks * light.intensity * attenuation;

		color += baseColor * diffuse + specular;
	}

	// Gamma-correction
	let corrected_color = pow(color, vec3f(2.2));
	return vec4f(corrected_color, uMyUniforms.color.a);
}
