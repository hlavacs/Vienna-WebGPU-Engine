// AUTO-GENERATED from LightStruct. Do not edit.
// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.

struct LightStruct {
	transform: mat4x4<f32>,
	color: vec3<f32>,
	intensity: f32,
	light_type: u32,
	spot_angle: f32,
	spot_softness: f32,
	range: f32,
	shadowIndex: u32,
	shadowCount: u32,
	_pad1: f32,
	_pad2: f32,
}
