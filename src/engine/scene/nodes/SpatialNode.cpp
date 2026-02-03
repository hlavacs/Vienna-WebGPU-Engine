#include "engine/scene/nodes/SpatialNode.h"
#include "engine/rendering/DebugRenderCollector.h"

namespace engine::scene::nodes
{
void SpatialNode::onDebugDraw(engine::rendering::DebugRenderCollector &collector)
{
	collector.addTransformAxes(m_transform.getWorldMatrix());

	// Call base class
	Node::onDebugDraw(collector);
}

Transform *SpatialNode::findSpatialParentTransform() const
{
	Node *currentParent = getParent();
	while (currentParent)
	{
		// Check if parent is a spatial node
		if (currentParent->isSpatial())
		{
			auto spatialParent = currentParent->asSpatialNode();
			return &spatialParent->getTransform();
		}
		// Move up the hierarchy
		currentParent = currentParent->getParent();
	}
	return nullptr; // No spatial parent found
}

void SpatialNode::updateTransformParent(bool keepWorld)
{
	// Find the nearest spatial parent in the Node hierarchy
	auto spatialParent = findSpatialParentTransform();

	// Update Transform parent using friend access
	m_transform.setParentInternal(spatialParent, keepWorld);

	// Propagate dirty state to all spatial children
	propagateTransformDirty();
}

void SpatialNode::propagateTransformDirty()
{
	m_transform.markDirty();

	// Recursively mark all spatial children as dirty
	for (const auto &child : children)
	{
		if (child && child->isSpatial())
		{
			auto spatialChild = child->asSpatialNode();
			spatialChild->propagateTransformDirty();
		}
	}
}

} // namespace engine::scene::nodes
