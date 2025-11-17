#pragma once
#include <glm/glm.hpp>

namespace engine::rendering
{

struct FrameUniforms
{
	glm::mat4 viewMatrix;
	glm::mat4 projectionMatrix;
	glm::vec3 cameraWorldPosition;
	float time;
};
static_assert(sizeof(FrameUniforms) % 16 == 0, "FrameUniforms must match shader layout");

} // namespace engine::rendering
