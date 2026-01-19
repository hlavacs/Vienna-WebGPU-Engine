struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec3f,
    @location(3) uv: vec2f,
    @location(4) tangent: vec3f,
    @location(5) bitangent: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) view_direction: vec3f,
    @location(4) tangent: vec3f,
    @location(5) bitangent: vec3f,
    @location(6) world_position: vec4f,
};

struct FrameUniforms {
    view_matrix: mat4x4f,
    projection_matrix: mat4x4f,
    view_projection_matrix: mat4x4f,
    camera_world_position: vec3f,
    time: f32,
};

struct Light {
    transform: mat4x4f,
    color: vec3f,
    intensity: f32,
    light_type: u32,
    // 0 = ambient, 1 = directional, 2 = point, 3 = spot
    spot_angle: f32,
    spot_softness: f32,
    range: f32,
    shadow_index: i32, // <0 = no shadows
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

struct LightsBuffer {
    count: u32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    lights: array<Light>,
};

struct ObjectUniforms {
    model_matrix: mat4x4f,
    normal_matrix: mat4x4f,
};

struct MaterialUniforms {
    diffuse: vec4f,
    emission: vec4f,
    transmittance: vec4f,
    ambient: vec4f,
    roughness: f32,
    metallic: f32,
    ior: f32,
    normal_strength: f32,
};

struct Shadow2DUniform {
    light_view_projection: mat4x4f,
    bias: f32,
    normal_bias: f32,
    texel_size: f32,
    pcf_kernel: u32,
};

struct ShadowCubeUniform {
    light_position: vec3f,
    bias: f32,
    texel_size: f32,
    pcf_kernel: u32,
    _pad: vec2f,
};

@group(0) @binding(0)
var<uniform> u_frame: FrameUniforms;

@group(1) @binding(0)
var<storage, read> u_lights: LightsBuffer;

@group(2) @binding(0)
var<uniform> u_object: ObjectUniforms;

@group(3) @binding(0)
var<uniform> u_material: MaterialUniforms;
@group(3) @binding(1)
var texture_sampler: sampler;
@group(3) @binding(2)
var base_color_texture: texture_2d<f32>;
@group(3) @binding(3)
var normal_texture: texture_2d<f32>;
@group(3) @binding(4)
var ao_texture: texture_2d<f32>;
@group(3) @binding(5)
var roughness_texture: texture_2d<f32>;
@group(3) @binding(6)
var metallic_texture: texture_2d<f32>;
@group(3) @binding(7)
var emission_texture: texture_2d<f32>;

@group(4) @binding(0)
var shadow_sampler: sampler_comparison;
@group(4) @binding(1)
var shadow_maps_2d: texture_depth_2d_array;
@group(4) @binding(2)
var shadow_maps_cube: texture_depth_cube_array;
@group(4) @binding(3)
var<storage, read> u_shadow_2d: array<Shadow2DUniform>;
@group(4) @binding(4)
var<storage, read> u_shadow_cube: array<ShadowCubeUniform>;

const PI: f32 = 3.141592653589793;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let world_position = u_object.model_matrix * vec4f(in.position, 1.0);
    out.world_position = world_position;
    out.position = u_frame.view_projection_matrix * world_position;
    out.normal = normalize((u_object.normal_matrix * vec4f(in.normal, 0.0)).xyz);
    out.tangent = normalize((u_object.normal_matrix * vec4f(in.tangent, 0.0)).xyz);
    out.bitangent = normalize((u_object.normal_matrix * vec4f(in.bitangent, 0.0)).xyz);
    out.color = in.color;
    out.uv = in.uv;
    out.view_direction = u_frame.camera_world_position - world_position.xyz;
    return out;
}

// ------------------------------------------------------------
// Utility
// ------------------------------------------------------------
fn saturate(v: f32) -> f32 {
    return clamp(v, 0.0, 1.0);
}

fn fresnel_schlick(cos_theta: f32, f0: vec3f) -> vec3f {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

fn distribution_ggx(n: vec3f, h: vec3f, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let n_dot_h = max(dot(n, h), 0.0);
    let n_dot_h2 = n_dot_h * n_dot_h;

    let denom = (n_dot_h2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom + 0.000001);
}

fn geometry_schlick_ggx(n_dot_v: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return n_dot_v / (n_dot_v * (1.0 - k) + k + 0.000001);
}

fn geometry_smith(n: vec3f, v: vec3f, l: vec3f, roughness: f32) -> f32 {
    let n_dot_v = max(dot(n, v), 0.0);
    let n_dot_l = max(dot(n, l), 0.0);
    let ggx1 = geometry_schlick_ggx(n_dot_v, roughness);
    let ggx2 = geometry_schlick_ggx(n_dot_l, roughness);
    return ggx1 * ggx2;
}

fn get_direction_from_transform(transform: mat4x4f) -> vec3f {
    return normalize(-transform[2].xyz);
}

fn get_position_from_transform(transform: mat4x4f) -> vec3f {
    return transform[3].xyz;
}

// ------------------------------------------------------------
// Shadow Mapping
// ------------------------------------------------------------
fn calculate_shadow(world_pos: vec3f, normal: vec3f, light: Light) -> f32 {
    if (light.shadow_index < 0) {
        return 1.0;
    }
    if (light.light_type == 1u || light.light_type == 3u) {
        let shadow = u_shadow_2d[u32(light.shadow_index)];

        let light_space_pos = shadow.light_view_projection * vec4f(world_pos, 1.0);
        var shadow_proj = light_space_pos.xyz / light_space_pos.w;
        var shadow_uv = shadow_proj.xy * 0.5 + vec2f(0.5);
		shadow_uv.y = 1.0 - shadow_uv.y;
        let shadow_depth = shadow_proj.z;

        if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||
            shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||
            shadow_depth < 0.0 || shadow_depth > 1.0) {
            return 1.0;
        }

        let light_dir = normalize(-light.transform[2].xyz);
        let ndotl = clamp(dot(normal, light_dir), 0.0, 1.0);
        let slope_bias = shadow.normal_bias * (1.0 - ndotl);
        var final_bias = (shadow.bias + slope_bias);
		if(light.light_type == 3u) {
        	final_bias *= shadow.texel_size;
		}
        final_bias = clamp(final_bias, 0.0000001, 0.003);
        let current_depth = shadow_depth - final_bias;
		
		// =============================
        // Directional horizon stabilization
        // =============================
        var pcf_scale = 1.0;
        var shadow_strength = 1.0;

        if (light.light_type == 1u) {
            let sun_dot_up = saturate(dot(light_dir, vec3f(0.0, 1.0, 0.0)));

			shadow_strength = smoothstep(0.0, 0.4, sun_dot_up);
			pcf_scale = mix(0.5, 1.0, shadow_strength);
        }

        var visibility = 0.0;
        let kernel = i32(shadow.pcf_kernel);
        var samples = 0.0;

        for (var x = -kernel; x <= kernel; x = x + 1) {
            for (var y = -kernel; y <= kernel; y = y + 1) {
                let offset = vec2f(f32(x), f32(y)) * shadow.texel_size * pcf_scale;
                let uv = shadow_uv + offset;
                // Let the sampler handle out-of-bounds sampling
                visibility += textureSampleCompare(shadow_maps_2d, shadow_sampler, uv, u32(light.shadow_index), current_depth);
                samples += 1.0;
            }
        }
		
		if (light.light_type == 1u) {
            return mix(1.0, visibility / samples, shadow_strength);
        }

        return visibility / samples;
    } else if (light.light_type == 2u) {
        let shadow = u_shadow_cube[u32(light.shadow_index)];

        // Linear distance from light to fragment
        let to_light = world_pos - shadow.light_position;
        let linear_depth = length(to_light);

        let current_depth = linear_depth - shadow.bias; // bias in world units
	
        var visibility = 0.0;
        let kernel = i32(shadow.pcf_kernel);
        var samples = 0.0;

        // Use simple cube PCF: sample small offsets in 3D directions
        for (var x = -kernel; x <= kernel; x = x + 1) {
            for (var y = -kernel; y <= kernel; y = y + 1) {
                for (var z = -kernel; z <= kernel; z = z + 1) {
                    let offset = vec3f(f32(x), f32(y), f32(z)) * shadow.texel_size;
                    let sample_dir = normalize(to_light + offset);
                    visibility += textureSampleCompare(shadow_maps_cube, shadow_sampler, sample_dir, u32(light.shadow_index), current_depth);
                    samples += 1.0;
                }
            }
		} 

        return visibility / samples;
    }

    return 1.0;
}

// ------------------------------------------------------------
// Fragment
// ------------------------------------------------------------
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let n = normalize(in.normal);
    let t = normalize(in.tangent - n * dot(n, in.tangent));
    let b = cross(n, t);
    let tbn = mat3x3f(t, b, n);

    let n_map: vec3f = textureSample(normal_texture, texture_sampler, in.uv).rgb * 2.0 - 1.0;
    let scaled_n = clamp(n_map.xy * u_material.normal_strength, vec2<f32>(-2.0, -2.0), vec2<f32>(2.0, 2.0));
    let normal = normalize(tbn * vec3<f32>(scaled_n.x, scaled_n.y, n_map.z));

    let v = normalize(in.view_direction);

    let base_color = textureSample(base_color_texture, texture_sampler, in.uv).rgb * u_material.diffuse.rgb;
    let roughness = clamp(textureSample(roughness_texture, texture_sampler, in.uv).r * u_material.roughness, 0.001, 1.0);
    let metallic = textureSample(metallic_texture, texture_sampler, in.uv).r * u_material.metallic;
    let ao_tex = textureSample(ao_texture, texture_sampler, in.uv).r;
    let ao = saturate(ao_tex);
    let emission = textureSample(emission_texture, texture_sampler, in.uv).rgb * u_material.emission.rgb * u_material.emission.w;

    let ior = max(u_material.ior, 1.0);
    let f0_dielectric = pow((ior - 1.0) / (ior + 1.0), 2.0);
    let f0 = mix(vec3f(f0_dielectric), base_color, metallic);

    var lo = vec3f(0.0);
    let world_pos = in.world_position.xyz;

    for (var i: u32 = 0u; i < u_lights.count; i = i + 1u) {
        let light = u_lights.lights[i];
        if (light.light_type == 0u) {
            lo += base_color * u_material.ambient.rgb * u_material.ambient.w * light.color * light.intensity * ao;
            continue;
        }

        var l = vec3f(0.0);
        var attenuation = 1.0;

        if (light.light_type == 1u) {
            l = get_direction_from_transform(light.transform);
        } else if (light.light_type == 2u) {
            let light_pos = get_position_from_transform(light.transform);
            let to_light = light_pos - world_pos;
            let dist = length(to_light);
            l = normalize(to_light);
            attenuation = 1.0 / max(dist * dist, 0.01);
        } else if (light.light_type == 3u) {
            let light_pos = get_position_from_transform(light.transform);
            let to_light = light_pos - world_pos;
            let dist = length(to_light);
            l = normalize(to_light);

            let spot_dir = get_direction_from_transform(light.transform);
            let cos_theta = dot(l, spot_dir);
            let inner_ratio = 1.0 - max(0.01, light.spot_softness);
            let cos_outer = cos(light.spot_angle);
            let cos_inner = cos(light.spot_angle * inner_ratio);
            let spot_effect = smoothstep(cos_outer, cos_inner, cos_theta);

            let dist_attenuation = 1.0 / max(dist * dist, 0.01);
            let range_factor = 1.0 - smoothstep(light.range * 0.75, light.range, dist);
            attenuation = select(0.0, spot_effect * dist_attenuation * range_factor, cos_theta > cos_outer);
        }

        let h = normalize(v + l);
        let n_dot_l = max(dot(normal, l), 0.0);
        let n_dot_v = max(dot(normal, v), 0.0);
        if (n_dot_l <= 0.0) { continue; }

        let shadow = calculate_shadow(world_pos, normal, light);

        let d = distribution_ggx(normal, h, roughness);
        let g = geometry_smith(normal, v, l, roughness);
        let f = fresnel_schlick(max(dot(h, v), 0.0), f0);

        let numerator = d * g * f;
        let denominator = max(4.0 * n_dot_v * n_dot_l, 0.001);
        let specular = numerator / denominator;

        let k_s = f;
        let k_d = (vec3f(1.0) - k_s) * (1.0 - metallic);

        let radiance = light.color * light.intensity * attenuation;
        lo += (k_d * base_color / PI + specular) * radiance * n_dot_l * shadow;
    }

    let color = lo + emission;
    return vec4f(color, u_material.diffuse.w);
}
