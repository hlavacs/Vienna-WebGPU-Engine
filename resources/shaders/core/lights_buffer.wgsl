// AUTO-GENERATED from LightsBuffer (header + runtime array<LightStruct>). Do not edit.
// Source-of-truth: C++ StructDescriptor. See doc/SpecShaderSystem.md.

#include "engine://core/light_struct.wgsl"

struct LightsBuffer {
	count: u32,
	_pad1: f32,
	_pad2: f32,
	_pad3: f32,
	lights: array<LightStruct>,
}
