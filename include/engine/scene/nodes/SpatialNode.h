#pragma once
#include "engine/scene/Transform.h"
#include "engine/scene/nodes/Node.h"
#include <memory>

namespace engine::scene::nodes
{
/**
 * @brief Base node for all spatial objects (has a transform).
 * Uses virtual inheritance to prevent diamond inheritance issues.
 *
 * SpatialNode maintains the Transform hierarchy by:
 * - Updating Transform parent when Node hierarchy changes
 * - Skipping non-spatial parent nodes in the Transform hierarchy
 * - Propagating Transform updates to spatial children
 */
class SpatialNode : public virtual nodes::Node
{
	// Allow Node to update transform hierarchy
	friend class Node;

  public:
	using Ptr = std::shared_ptr<SpatialNode>;

	SpatialNode(std::optional<std::string> name = std::nullopt) : Node(name)
	{
		addNodeType(NodeType::Spatial);
		m_transform = std::make_shared<Transform>();
	}
	~SpatialNode() override = default;

	std::shared_ptr<Transform> getTransform() { return m_transform; }
	void setTransform(const std::shared_ptr<Transform> &t) { m_transform = t; }

	/** @brief Override to draw transform axes when debug is enabled */
	void onDebugDraw(engine::rendering::DebugRenderCollector &collector) override;

  protected:
	std::shared_ptr<Transform> m_transform;

	/**
	 * @brief Helper to find the nearest spatial ancestor.
	 * Traverses up the Node hierarchy until a SpatialNode is found.
	 * @return Transform of the nearest spatial parent, or nullptr if none exists.
	 */
	std::shared_ptr<Transform> findSpatialParentTransform() const;

	/**
	 * @brief Updates the Transform parent to match Node hierarchy.
	 * Called internally when Node hierarchy changes.
	 * @param keepWorld If true, maintains world-space transform when reparenting.
	 */
	void updateTransformParent(bool keepWorld = true);

	/**
	 * @brief Propagates Transform dirty state to spatial children.
	 * Recursively marks all spatial children's transforms as dirty.
	 */
	void propagateTransformDirty();
};
} // namespace engine::scene::nodes
