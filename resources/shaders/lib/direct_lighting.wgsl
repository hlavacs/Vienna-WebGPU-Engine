// Direct (analytic) light evaluation shared by the deferred composition
// (opaque) and the forward PBR (transparent) paths. Single source of truth so
// the two can't drift onto different lighting models again — previously the
// deferred path used a simplified Blinn-Phong while the forward path used full
// Cook-Torrance GGX, which made transparent surfaces light visibly differently
// from the opaque geometry behind them the moment any non-ambient light was on.
//
// Depends on the consumer having pulled in the canonical Scene bindings (for
// LightStruct) — same contract as lib/shadow.wgsl. NOT included by shaders that
// don't define LightStruct (e.g. skybox), which is why this lives separately
// from lib/lighting.wgsl.

#include "engine://lib/lighting.wgsl"

// Cook-Torrance GGX contribution of one NON-AMBIENT light (directional / point
// / spot), BEFORE shadow attenuation — the caller multiplies the result by its
// shadow factor. Ambient lights (light_type 0) are handled by each shader
// directly because the deferred G-buffer carries no per-material ambient term.
//
// `transmission` is the material's transmittance alpha (0 = none): it removes
// the corresponding share of the diffuse lobe, since light that passes through
// the surface can't also scatter diffusely off it. The deferred path has no
// transmission data and passes 0, leaving opaque diffuse untouched.
fn evaluate_direct_light_ggx(
	light: LightStruct,
	N: vec3<f32>,
	V: vec3<f32>,
	world_pos: vec3<f32>,
	base_color: vec3<f32>,
	f0: vec3<f32>,
	roughness: f32,
	metallic: f32,
	transmission: f32
) -> vec3<f32> {
	var L = vec3<f32>(0.0);
	var attenuation = 1.0;

	if (light.light_type == 1u) {
		// Directional
		L = get_direction_from_transform(light.transform);
	} else if (light.light_type == 2u) {
		// Point. Inverse-square falloff with a smooth cutoff at the light's
		// range so distant lights don't keep contributing (and so the forward
		// path, which sums a cluster's lights, can't be brightened by a light
		// whose volume barely clips the froxel).
		let light_pos = get_position_from_transform(light.transform);
		let to_light = light_pos - world_pos;
		let dist = length(to_light);
		L = normalize(to_light);
		let range_factor = 1.0 - smoothstep(light.range * 0.75, light.range, dist);
		attenuation = (1.0 / max(dist * dist, 0.001)) * range_factor;
	} else if (light.light_type == 3u) {
		// Spot
		let light_pos = get_position_from_transform(light.transform);
		let to_light = light_pos - world_pos;
		let dist = length(to_light);
		L = normalize(to_light);

		let spot_dir = get_direction_from_transform(light.transform);
		let cos_theta = dot(L, spot_dir);
		let inner_ratio = 1.0 - max(0.01, light.spot_softness);
		let cos_outer = cos(light.spot_angle);
		let cos_inner = cos(light.spot_angle * inner_ratio);
		let spot_effect = smoothstep(cos_outer, cos_inner, cos_theta);

		let dist_attenuation = 1.0 / max(dist * dist, 0.01);
		let range_factor = 1.0 - smoothstep(light.range * 0.75, light.range, dist);
		attenuation = select(0.0, spot_effect * dist_attenuation * range_factor, cos_theta > cos_outer);
	}

	let n_dot_l = max(dot(N, L), 0.0);
	if (n_dot_l <= 0.0) {
		return vec3<f32>(0.0);
	}
	let n_dot_v = max(dot(N, V), 0.0);
	let H = normalize(V + L);

	let d = distribution_ggx(N, H, roughness);
	let g = geometry_smith(N, V, L, roughness);
	let f = fresnel_schlick(max(dot(H, V), 0.0), f0);

	let specular = (d * g * f) / max(4.0 * n_dot_v * n_dot_l, 0.001);

	let k_s = f;
	let k_d = (vec3<f32>(1.0) - k_s) * (1.0 - metallic) * (1.0 - clamp(transmission, 0.0, 1.0));

	let radiance = light.color * light.intensity * attenuation;
	return (k_d * base_color / LIGHTING_PI + specular) * radiance * n_dot_l;
}
