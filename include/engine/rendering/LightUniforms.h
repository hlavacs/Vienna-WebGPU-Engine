#pragma once
#include <glm/glm.hpp>

namespace engine::rendering
{

struct LightStruct
{
	glm::mat4 transform = glm::mat4(1.0f);
	glm::vec3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	uint32_t light_type = 0;
	float spot_angle = 0.5f;
	float spot_softness = 0.2f;
	float _pad = 0.0f;
};
static_assert(sizeof(LightStruct) % 16 == 0, "LightStruct must match WGSL layout");

struct LightsBuffer
{
	uint32_t count = 0;
	float pad3[3];
};
static_assert(sizeof(LightsBuffer) % 16 == 0, "LightsBuffer must match WGSL layout");

} // namespace engine::rendering
