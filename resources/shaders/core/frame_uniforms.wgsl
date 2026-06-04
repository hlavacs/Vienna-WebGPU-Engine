// AUTO-GENERATED from FrameUniforms. Do not edit.
// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.

struct FrameUniforms {
	viewMatrix: mat4x4<f32>,
	projectionMatrix: mat4x4<f32>,
	viewProjectionMatrix: mat4x4<f32>,
	cameraWorldPosition: vec3<f32>,
	time: f32,
}

@group(0) @binding(0)
var<uniform> u_frame: FrameUniforms;
