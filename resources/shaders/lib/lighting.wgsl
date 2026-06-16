// Lighting helpers shared by every shader that does PBR lighting.
//
// Hand-written; lives under lib/ (not core/) because none of these are
// derivable from a C++ struct — they're math utilities for the shader path.

fn saturate(v: f32) -> f32 {
	return clamp(v, 0.0, 1.0);
}

// Forward (-Z column) of a 4x4 transform — used as the light direction for
// directional / spot lights so we can encode the light orientation in the
// same `transform` matrix all lights share.
fn get_direction_from_transform(transform: mat4x4<f32>) -> vec3<f32> {
	return normalize(-transform[2].xyz);
}

// Translation column of a 4x4 transform.
fn get_position_from_transform(transform: mat4x4<f32>) -> vec3<f32> {
	return transform[3].xyz;
}

// Equirect lat/long projection: world-space direction -> UV in the HDR env map.
// Used for the IBL irradiance sample and (eventually) reflection probes.
const LIGHTING_PI:    f32 = 3.141592653589793;
const LIGHTING_INV_PI: f32 = 0.31830988618;

fn direction_to_equirect_uv(direction: vec3<f32>) -> vec2<f32> {
	let dir = normalize(direction);
	let u = atan2(dir.z, dir.x) * (0.5 * LIGHTING_INV_PI) + 0.5;
	let v = acos(clamp(dir.y, -1.0, 1.0)) * LIGHTING_INV_PI;
	return vec2<f32>(u, v);
}

// Fresnel-Schlick approximation. F0 is the reflectance at normal incidence:
// 0.04 (≈ 4%) for dielectrics, the base color for metals. The fifth-power
// term boosts reflection at grazing angles, which is the entire reason
// non-metals show edge specular while staying matte in the middle.
fn fresnel_schlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
	let oneMinusCos = clamp(1.0 - cosTheta, 0.0, 1.0);
	return F0 + (vec3<f32>(1.0) - F0) * pow(oneMinusCos, 5.0);
}

// Roughness-aware variant: roughness reduces grazing-angle fresnel as the
// surface becomes more diffuse. Used for IBL specular so a fully-rough
// surface fades into matte instead of having a hot rim. F-term reuses the
// max(1-rough, F0) trick from UE4's Real Shading paper — keeps the metal
// reflection tint while dampening the dielectric rim with roughness.
fn fresnel_schlick_roughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
	let oneMinusCos = clamp(1.0 - cosTheta, 0.0, 1.0);
	let invRough    = vec3<f32>(1.0 - roughness);
	return F0 + (max(invRough, F0) - F0) * pow(oneMinusCos, 5.0);
}

// --- Cook-Torrance GGX microfacet terms -------------------------------------
// Pure math (no light/scene types), so they're safe to live here in lib and be
// pulled into every lighting shader. The full per-light evaluation that uses
// them lives in lib/direct_lighting.wgsl (it needs LightStruct, which not every
// includer of this file defines).

// GGX/Trowbridge-Reitz normal distribution.
fn distribution_ggx(n: vec3<f32>, h: vec3<f32>, roughness: f32) -> f32 {
	let a = roughness * roughness;
	let a2 = a * a;
	let n_dot_h = max(dot(n, h), 0.0);
	let n_dot_h2 = n_dot_h * n_dot_h;

	let denom = (n_dot_h2 * (a2 - 1.0) + 1.0);
	return a2 / (LIGHTING_PI * denom * denom + 0.000001);
}

// Schlick-GGX geometry term for a single direction (uses the direct-light k).
fn geometry_schlick_ggx(n_dot_v: f32, roughness: f32) -> f32 {
	let r = roughness + 1.0;
	let k = (r * r) / 8.0;
	return n_dot_v / (n_dot_v * (1.0 - k) + k + 0.000001);
}

// Smith geometry term: masking + shadowing combined for view and light dirs.
fn geometry_smith(n: vec3<f32>, v: vec3<f32>, l: vec3<f32>, roughness: f32) -> f32 {
	let n_dot_v = max(dot(n, v), 0.0);
	let n_dot_l = max(dot(n, l), 0.0);
	let ggx1 = geometry_schlick_ggx(n_dot_v, roughness);
	let ggx2 = geometry_schlick_ggx(n_dot_l, roughness);
	return ggx1 * ggx2;
}
