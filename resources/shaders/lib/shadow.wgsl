// Shadow sampling helpers shared between PBR forward and deferred composition.
//
// Depends on the consumer having pulled in the canonical Scene bindings — the
// helpers reference `u_shadows`, `shadow_maps_2d`, `shadow_maps_cube`,
// `shadow_sampler` (all at @group(1)) plus `u_frame` (@group(0)) for the
// view-space cascade selection.
//
// Algorithm matches what was previously hand-duplicated in PBR_Lit_Shader
// (snake_case) and deferred_composition (camelCase); migrating both onto this
// single source eliminates the drift that would otherwise sneak in next time
// someone tweaks the cascade math.

#include "engine://lib/lighting.wgsl"

fn select_cascade(view_depth: f32, light: LightStruct) -> u32 {
	for (var i: u32 = 0u; i < light.shadowCount; i = i + 1u) {
		let shadow_idx = light.shadowIndex + i;
		let shadow = u_shadows[shadow_idx];
		if (view_depth <= shadow.cascadeSplit) {
			return i;
		}
	}
	return light.shadowCount - 1u;
}

fn calculate_shadow(world_pos: vec3<f32>, normal: vec3<f32>, light: LightStruct) -> f32 {
	if (light.shadowCount == 0u) {
		return 1.0;
	}

	// Directional (1) + spot (3): 2D shadow map. CSM picks a cascade for
	// directional based on view-space depth; spots get one slice each.
	if (light.light_type == 1u || light.light_type == 3u) {
		var shadow_index = light.shadowIndex;

		if (light.light_type == 1u && light.shadowCount > 1u) {
			let view_pos   = u_frame.viewMatrix * vec4<f32>(world_pos, 1.0);
			let view_depth = -view_pos.z;
			let cascade_idx = select_cascade(view_depth, light);
			shadow_index = light.shadowIndex + cascade_idx;
		}

		let shadow = u_shadows[shadow_index];
		let light_space_pos = shadow.viewProj * vec4<f32>(world_pos, 1.0);
		if (light_space_pos.w <= 0.00001) {
			return 1.0;
		}

		let shadow_proj = light_space_pos.xyz / light_space_pos.w;
		let shadow_uv   = shadow_proj.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
		let shadow_depth = shadow_proj.z;
		if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||
			shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||
			shadow_depth <= 0.0 || shadow_depth >= 1.0) {
			return 1.0;
		}

		// Slope-scaled bias keeps acne off oblique surfaces; the directional
		// branch uses an absolute texel floor, spots fall off with distance.
		let light_dir  = normalize(get_direction_from_transform(light.transform));
		let ndotl      = max(dot(normal, light_dir), 0.0);
		let slope_bias = shadow.normalBias * (1.0 - ndotl);
		var final_bias: f32;
		if (light.light_type == 1u) {
			let texel_bias = shadow.bias * shadow.texelSize;
			final_bias = max(texel_bias, 0.001) + max(slope_bias * shadow.texelSize, 0.001);
		} else {
			let depth_scale    = abs(shadow_depth);
			let distance_scale = smoothstep(shadow.near, shadow.far, depth_scale);
			final_bias = (shadow.bias + slope_bias) * distance_scale;
		}
		final_bias = clamp(final_bias, 0.000001, 0.02);
		let current_depth = shadow_depth - final_bias;

		// Horizon stabilisation: when a directional light grazes the horizon
		// (sun setting), shrink the PCF kernel + fade shadow strength so the
		// scene doesn't suddenly turn pitch black on the last lit frame.
		var pcf_scale       = 1.0;
		var shadow_strength = 1.0;
		if (light.light_type == 1u) {
			let sun_dot_up  = saturate(dot(light_dir, vec3<f32>(0.0, 1.0, 0.0)));
			shadow_strength = smoothstep(0.0, 0.4, sun_dot_up);
			pcf_scale       = mix(0.5, 1.0, shadow_strength);
		}

		var visibility = 0.0;
		let kernel     = i32(shadow.pcfKernel);
		var samples    = 0.0;
		for (var x = -kernel; x <= kernel; x = x + 1) {
			for (var y = -kernel; y <= kernel; y = y + 1) {
				let offset = vec2<f32>(f32(x), f32(y)) * shadow.texelSize * pcf_scale;
				let uv     = shadow_uv + offset;
				visibility += textureSampleCompare(shadow_maps_2d, shadow_sampler, uv, shadow.textureIndex, current_depth);
				samples += 1.0;
			}
		}

		if (light.light_type == 1u) {
			return mix(1.0, visibility / samples, shadow_strength);
		}
		return visibility / samples;
	}

	// Point light (2): cube shadow map.
	if (light.light_type == 2u) {
		let shadow = u_shadows[light.shadowIndex];
		let to_frag      = (world_pos - shadow.lightPos) * vec3<f32>(-1.0, 1.0, 1.0);
		let linear_depth = length(to_frag);
		if (linear_depth >= shadow.far) {
			return 1.0;
		}

		let sample_dir = normalize(to_frag);
		let ndotl      = max(dot(normal, sample_dir), 0.0);
		let slope_bias = shadow.normalBias * (1.0 - ndotl);
		let final_bias = shadow.bias + slope_bias;
		let current_depth = clamp((linear_depth - final_bias) / shadow.far, 0.0, 1.0);

		var visibility = 0.0;
		var samples    = 0.0;
		let kernel = i32(shadow.pcfKernel);
		let radius = shadow.texelSize * (linear_depth / shadow.far);
		let w = sample_dir;
		var up = vec3<f32>(0.0, 1.0, 0.0);
		if (abs(w.y) > 0.999) { up = vec3<f32>(1.0, 0.0, 0.0); }
		let u = normalize(cross(up, w));
		let v = cross(w, u);
		for (var x = -kernel; x <= kernel; x = x + 1) {
			for (var y = -kernel; y <= kernel; y = y + 1) {
				for (var z = -kernel; z <= kernel; z = z + 1) {
					let offset             = u * f32(x) + v * f32(y) + w * f32(z);
					let sample_dir_offset  = normalize(to_frag + offset * radius);
					visibility += textureSampleCompare(shadow_maps_cube, shadow_sampler, sample_dir_offset, shadow.textureIndex, current_depth);
					samples += 1.0;
				}
			}
		}
		return visibility / samples;
	}

	return 1.0;
}
