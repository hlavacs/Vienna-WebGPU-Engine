// AUTO-GENERATED from ObjectUniforms. Do not edit.
// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.

struct ObjectUniforms {
	modelMatrix: mat4x4<f32>,
	normalMatrix: mat4x4<f32>,
}

@group(3) @binding(0)
var<uniform> u_object: ObjectUniforms;
