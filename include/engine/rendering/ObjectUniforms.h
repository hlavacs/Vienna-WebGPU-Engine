#pragma once
#include <glm/glm.hpp>

namespace engine::rendering
{

struct ObjectUniforms
{
	glm::mat4 modelMatrix;
	glm::mat4 normalMatrix;
};
static_assert(sizeof(ObjectUniforms) % 16 == 0, "ObjectUniforms must match shader layout");

} // namespace engine::rendering
