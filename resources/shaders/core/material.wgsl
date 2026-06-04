// AUTO-GENERATED from PBRProperties. Do not edit.
// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.

struct PBRProperties {
	diffuse: vec4<f32>,
	emission: vec4<f32>,
	transmittance: vec4<f32>,
	ambient: vec4<f32>,
	roughness: f32,
	metallic: f32,
	ior: f32,
	normalTextureScale: f32,
	alphaMode: u32,
	alphaCutoff: f32,
	_alphaPad0: u32,
	_alphaPad1: u32,
}

@group(2) @binding(0)
var<uniform> u_material: PBRProperties;
