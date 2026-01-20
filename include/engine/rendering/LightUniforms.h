#pragma once
#include <glm/glm.hpp>
#include "ShadowUniforms.h"

namespace engine::rendering
{

struct LightStruct
{
	glm::mat4 transform = glm::mat4(1.0f); //< 64 bytes
	glm::vec3 color = {1.0f, 1.0f, 1.0f};  //< 12 bytes
	float intensity = 1.0f;				   //< 4 bytes (total: 80 bytes)
	uint32_t light_type = 0;			   //< 4 bytes (0 = ambient, 1 = directional, 2 = point, 3 = spot)
	float spot_angle = 0.5f;			   //< 4 bytes
	float spot_softness = 0.2f;			   //< 4 bytes
	float range = 10.0f;				   //< 4 bytes (total: 96 bytes)
	uint32_t shadowIndex = 0;			   //< 4 bytes (first index into u_shadows, 0 = no shadow)
	uint32_t shadowCount = 0;			   //< 4 bytes (number of shadows this light uses)
	float _pad1 = 0.0f;					   //< 4 bytes
	float _pad2 = 0.0f;					   //< 4 bytes (total: 112 bytes, aligned to 16)
};
static_assert(sizeof(LightStruct) % 16 == 0, "LightStruct must match WGSL layout");

struct LightsBuffer
{
	uint32_t count = 0;
	float _pad1 = 0.0f;
	float _pad2 = 0.0f;
	float _pad3 = 0.0f;
};
static_assert(sizeof(LightsBuffer) % 16 == 0, "LightsBuffer must match WGSL layout");

} // namespace engine::rendering
