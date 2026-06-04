#include "engine/rendering/shaders/EngineStructDescriptors.h"

#include "engine/rendering/EnvironmentUniforms.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/ShadowUniforms.h"
#include "engine/rendering/shaders/EngineCoreRegistry.h"
#include "engine/rendering/shaders/GpuStructTraits.h"

// One block per engine GPU struct: a token-stringified traits spec via
// ENGINE_GPU_STRUCT (types + offsets come from the C++ struct itself via
// aggregate_reflect), and a REGISTER_ENGINE_UNIFORM(_AS) line that registers
// the binding + file with the codegen registry.
//
// Adding a new engine struct = these two lines + a `#include` of the C++
// header. No path math, no per-struct emission code, no manual descriptor
// build. The registry's regenerateAll() (called from engine init) handles the
// rest, and the post-codegen validator picks the same registry up.

ENGINE_GPU_STRUCT(engine::rendering::FrameUniforms,
	viewMatrix,
	projectionMatrix,
	viewProjectionMatrix,
	cameraWorldPosition,
	time);

REGISTER_ENGINE_UNIFORM(engine::rendering::FrameUniforms, Frame)

ENGINE_GPU_STRUCT(engine::rendering::ObjectUniforms,
	modelMatrix,
	normalMatrix);

REGISTER_ENGINE_UNIFORM(engine::rendering::ObjectUniforms, Object)

// PBRProperties: the auto-derive rule would emit `u_pbr_properties` /
// `pbr_properties.wgsl`; engine convention uses `u_material` + `material.wgsl`,
// so the explicit _AS form pins both.
ENGINE_GPU_STRUCT(engine::rendering::PBRProperties,
	diffuse,
	emission,
	transmittance,
	ambient,
	roughness,
	metallic,
	ior,
	normalTextureScale,
	alphaMode,
	alphaCutoff,
	_alphaPad0,
	_alphaPad1);

REGISTER_ENGINE_UNIFORM_AS(engine::rendering::PBRProperties, Material, "u_material", "material.wgsl")

// Scene-group anchors live at non-zero @binding(N) slots so they get the
// struct-only emit path: codegen writes `struct X { ... }` into core/<file>,
// but the @group(1) @binding(N) declaration stays in each consuming shader
// (PBR forward / deferred composition each pick their own binding indices).

ENGINE_GPU_STRUCT(engine::rendering::EnvironmentUniforms,
	params);

REGISTER_ENGINE_STRUCT(engine::rendering::EnvironmentUniforms, "environment_uniforms.wgsl")

ENGINE_GPU_STRUCT(engine::rendering::ShadowUniform,
	viewProj,
	lightPos,
	near,
	far,
	bias,
	normalBias,
	texelSize,
	pcfKernel,
	shadowType,
	textureIndex,
	cascadeSplit);

REGISTER_ENGINE_STRUCT(engine::rendering::ShadowUniform, "shadow_uniform.wgsl")

ENGINE_GPU_STRUCT(engine::rendering::LightStruct,
	transform,
	color,
	intensity,
	light_type,
	spot_angle,
	spot_softness,
	range,
	shadowIndex,
	shadowCount,
	_pad1,
	_pad2);

REGISTER_ENGINE_STRUCT(engine::rendering::LightStruct, "light_struct.wgsl")

ENGINE_GPU_STRUCT(engine::rendering::LightsBuffer,
	count,
	_pad1,
	_pad2,
	_pad3);

// 16-byte header + runtime array<LightStruct>. Replaces what used to be a
// hand-written `core/lights_buffer.wgsl`. Canonical Scene placement matches
// scene_bindings.wgsl's hand-emitted @group(1) @binding(0) line so the
// validator can confirm consumers (PBR forward, deferred composition, cluster
// compute) all bind it at the same slot.
REGISTER_ENGINE_BUFFER_WRAPPER(engine::rendering::LightsBuffer,
                               engine::rendering::LightStruct,
                               "lights",
                               "lights_buffer.wgsl",
                               Scene, 0, "u_lights")

namespace engine::rendering::shaders
{

const StructDescriptor &describeFrameUniforms()
{
	return gpuStructDescriptorOf<engine::rendering::FrameUniforms>();
}

const std::vector<GeneratedBindingRecord> &registeredGeneratedBindings()
{
	return core::EngineCoreRegistry::validatorView();
}

uint32_t regenerateEngineGeneratedFiles()
{
	return core::EngineCoreRegistry::regenerateAll();
}

} // namespace engine::rendering::shaders
