// AUTO-GENERATED from ShadowUniform. Do not edit.
// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.

struct ShadowUniform {
	viewProj: mat4x4<f32>,
	lightPos: vec3<f32>,
	near: f32,
	far: f32,
	bias: f32,
	normalBias: f32,
	texelSize: f32,
	pcfKernel: u32,
	shadowType: u32,
	textureIndex: u32,
	cascadeSplit: f32,
}
