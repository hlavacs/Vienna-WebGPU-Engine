#pragma once
#include "engine/scene/Transform.h"
#include "engine/scene/nodes/Node.h"
#include <memory>

namespace engine::scene::nodes
{
/**
 * @brief Base node for all spatial objects (has a transform).
 * Uses virtual inheritance to prevent diamond inheritance issues.
 */
class SpatialNode : public virtual nodes::Node
{
  public:
	using Ptr = std::shared_ptr<SpatialNode>;

	SpatialNode();
	virtual ~SpatialNode() = default;

	std::shared_ptr<Transform> getTransform() { return m_transform; }
	void setTransform(const std::shared_ptr<Transform> &t) { m_transform = t; }

	/** @brief Override to draw transform axes when debug is enabled */
	void onDebugDraw(engine::rendering::DebugRenderCollector &collector) override;

  protected:
	std::shared_ptr<Transform> m_transform;
};
} // namespace engine::scene::nodes
