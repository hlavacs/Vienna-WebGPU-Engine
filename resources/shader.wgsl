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
	transform: mat4x4f,
	color: vec3f,
	intensity: f32,
	light_type: u32,
	spot_angle: f32,
	spot_softness: f32,
	pad2: f32
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
			L = getDirectionFromTransform(light.transform);
		}
		else if (light.light_type == 1u) {
			// Point
			let lightPos = getPositionFromTransform(light.transform);
			L = normalize(lightPos - worldPos);
			let dist = length(lightPos - worldPos);
			attenuation = 1.0 / (dist * dist);
		}
		else if (light.light_type == 2u) {
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

		color += baseColor * diffuse + specular;
	}

	// Gamma-correction
	let corrected_color = pow(color, vec3f(2.2));
	return vec4f(corrected_color, uMyUniforms.color.a);
}
