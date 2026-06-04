// AUTO-GENERATED from Scene_BindGroup (struct includes + binding declarations). Do not edit.
// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.

#include "engine://core/lights_buffer.wgsl"
#include "engine://core/shadow_uniform.wgsl"
#include "engine://core/environment_uniforms.wgsl"
#include "engine://core/cluster_light_list.wgsl"

@group(1) @binding(0)  var<storage, read> u_lights:                 LightsBuffer;
@group(1) @binding(1)  var                shadow_sampler:           sampler_comparison;
@group(1) @binding(2)  var                shadow_maps_2d:           texture_depth_2d_array;
@group(1) @binding(3)  var                shadow_maps_cube:         texture_depth_cube_array;
@group(1) @binding(4)  var<storage, read> u_shadows:                array<ShadowUniform>;
@group(1) @binding(5)  var<uniform>       u_environment:            EnvironmentUniforms;
@group(1) @binding(6)  var                environment_sampler:      sampler;
@group(1) @binding(7)  var                environment_texture:      texture_2d<f32>;
@group(1) @binding(8)  var<storage, read> u_cluster_grid:           array<ClusterLightList>;
@group(1) @binding(9)  var<storage, read> u_cluster_light_indices:  array<u32>;
@group(1) @binding(10) var                brdf_lut:                 texture_2d<f32>;
@group(1) @binding(11) var                prefiltered_env:          texture_2d<f32>;
@group(1) @binding(12) var                irradiance_map:           texture_2d<f32>;
