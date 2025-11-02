#pragma once
#include "engine/scene/SpatialNode.h"
#include <glm/glm.hpp>

namespace engine::scene {
/**
 * @brief Node representing a light in the scene.
 */
class LightNode : public SpatialNode {
public:
	using Ptr = std::shared_ptr<LightNode>;
	LightNode();
	virtual ~LightNode() = default;

	// Example light properties
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;
	// Add more light-specific properties as needed
};
} // namespace engine::scene
