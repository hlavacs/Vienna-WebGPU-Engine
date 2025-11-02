#pragma once
#include "engine/scene/entity/Node.h"
#include "engine/scene/Transform.h"
#include <memory>

namespace engine::scene {
/**
 * @brief Base node for all spatial objects (has a transform).
 * Uses virtual inheritance to prevent diamond inheritance issues.
 */
class SpatialNode : public virtual entity::Node {
public:
	using Ptr = std::shared_ptr<SpatialNode>;
	SpatialNode();
	virtual ~SpatialNode() = default;

	std::shared_ptr<Transform> getTransform() { return m_transform; }
	void setTransform(const std::shared_ptr<Transform>& t) { m_transform = t; }
protected:
	std::shared_ptr<Transform> m_transform;
};
} // namespace engine::scene
