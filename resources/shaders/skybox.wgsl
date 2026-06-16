#include "engine://core/frame_uniforms.wgsl"

#include "engine://core/environment_uniforms.wgsl"

@group(4) @binding(0)
var<uniform> u_environment: EnvironmentUniforms;
@group(4) @binding(1)
var environment_sampler: sampler;
@group(4) @binding(2)
var environment_texture: texture_2d<f32>;

struct VertexOut {
	@builtin(position) clip_position: vec4f,
	@location(0) direction: vec3f,
}

#include "engine://lib/lighting.wgsl"

fn get_cube_vertex(vertex_index: u32) -> vec3f {
	// Naga validation in the current backend rejects runtime indexing of this const array,
	// so we use an explicit switch table for the 36 cube vertices.
	switch (vertex_index) {
		case 0u: { return vec3f(1.0, -1.0, -1.0); }
		case 1u: { return vec3f(1.0, -1.0, 1.0); }
		case 2u: { return vec3f(1.0, 1.0, 1.0); }
		case 3u: { return vec3f(1.0, -1.0, -1.0); }
		case 4u: { return vec3f(1.0, 1.0, 1.0); }
		case 5u: { return vec3f(1.0, 1.0, -1.0); }
		case 6u: { return vec3f(-1.0, -1.0, 1.0); }
		case 7u: { return vec3f(-1.0, -1.0, -1.0); }
		case 8u: { return vec3f(-1.0, 1.0, -1.0); }
		case 9u: { return vec3f(-1.0, -1.0, 1.0); }
		case 10u: { return vec3f(-1.0, 1.0, -1.0); }
		case 11u: { return vec3f(-1.0, 1.0, 1.0); }
		case 12u: { return vec3f(-1.0, 1.0, -1.0); }
		case 13u: { return vec3f(1.0, 1.0, -1.0); }
		case 14u: { return vec3f(1.0, 1.0, 1.0); }
		case 15u: { return vec3f(-1.0, 1.0, -1.0); }
		case 16u: { return vec3f(1.0, 1.0, 1.0); }
		case 17u: { return vec3f(-1.0, 1.0, 1.0); }
		case 18u: { return vec3f(-1.0, -1.0, 1.0); }
		case 19u: { return vec3f(1.0, -1.0, 1.0); }
		case 20u: { return vec3f(1.0, -1.0, -1.0); }
		case 21u: { return vec3f(-1.0, -1.0, 1.0); }
		case 22u: { return vec3f(1.0, -1.0, -1.0); }
		case 23u: { return vec3f(-1.0, -1.0, -1.0); }
		case 24u: { return vec3f(1.0, -1.0, 1.0); }
		case 25u: { return vec3f(-1.0, -1.0, 1.0); }
		case 26u: { return vec3f(-1.0, 1.0, 1.0); }
		case 27u: { return vec3f(1.0, -1.0, 1.0); }
		case 28u: { return vec3f(-1.0, 1.0, 1.0); }
		case 29u: { return vec3f(1.0, 1.0, 1.0); }
		case 30u: { return vec3f(-1.0, -1.0, -1.0); }
		case 31u: { return vec3f(1.0, -1.0, -1.0); }
		case 32u: { return vec3f(1.0, 1.0, -1.0); }
		case 33u: { return vec3f(-1.0, -1.0, -1.0); }
		case 34u: { return vec3f(1.0, 1.0, -1.0); }
		default: { return vec3f(-1.0, 1.0, -1.0); }
	}
}

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOut {
	var out: VertexOut;
	let local_pos = get_cube_vertex(vertex_index);

	let view_rotation = mat4x4f(
		vec4f(u_frame.viewMatrix[0].xyz, 0.0),
		vec4f(u_frame.viewMatrix[1].xyz, 0.0),
		vec4f(u_frame.viewMatrix[2].xyz, 0.0),
		vec4f(0.0, 0.0, 0.0, 1.0)
	);

	let clip = u_frame.projectionMatrix * view_rotation * vec4f(local_pos, 1.0);
	out.clip_position = clip.xyww;
	out.direction = local_pos;
	return out;
}

//@depth(compare="LessEqual", write=false)
@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4f {
	if (u_environment.params.z < 0.5) {
		return vec4f(0.0, 0.0, 0.0, 1.0);
	}

	// Scale the visible sky by the same intensity the IBL terms apply
	// (params.y). With raw HDR equirects the sun / sky values can spike
	// 10–50× linear; without this multiplier the skybox blows out the
	// framebuffer pre-tonemap and visually drowns out every lit surface,
	// while the IBL contributions on those same surfaces stay dimmed. A
	// single intensity knob keeps environment brightness coherent across
	// the sky background and the materials it lights.
	let uv = direction_to_equirect_uv(in.direction);
	let color = textureSample(environment_texture, environment_sampler, uv).rgb;
	return vec4f(color * u_environment.params.y, 1.0);
}
