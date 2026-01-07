#pragma once
#include <glm/glm.hpp>

namespace engine::rendering
{

struct ShadowUniforms
{
	glm::mat4 lightViewProjectionMatrix = glm::mat4(1.0f);
};
static_assert(sizeof(ShadowUniforms) % 16 == 0, "ShadowUniforms must match WGSL layout");

struct ShadowCubeUniforms
{
	glm::vec3 lightPosition = glm::vec3(0.0f);
	float farPlane = 100.0f;
};
static_assert(sizeof(ShadowCubeUniforms) % 16 == 0, "ShadowCubeUniforms must match WGSL layout");
} // namespace engine::rendering
