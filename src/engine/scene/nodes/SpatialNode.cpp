#include "engine/scene/nodes/SpatialNode.h"
#include "engine/rendering/DebugCollector.h"

namespace engine::scene::nodes
{
void SpatialNode::onDebugDraw(engine::rendering::DebugRenderCollector &collector)
{
	// Draw transform axes
	if (m_transform)
	{
		collector.addTransformAxes(m_transform->getWorldMatrix());
	}

	// Call base class
	Node::onDebugDraw(collector);
}

std::shared_ptr<Transform> SpatialNode::findSpatialParentTransform() const
{
	Node *currentParent = getParent();
	while (currentParent)
	{
		// Check if parent is a spatial node
		if (currentParent->isSpatial())
		{
			auto spatialParent = std::dynamic_pointer_cast<SpatialNode>(currentParent->shared_from_this());
			if (spatialParent && spatialParent->getTransform())
			{
				return spatialParent->getTransform();
			}
		}
		// Move up the hierarchy
		currentParent = currentParent->getParent();
	}
	return nullptr; // No spatial parent found
}

void SpatialNode::updateTransformParent(bool keepWorld)
{
	if (!m_transform)
		return;

	// Find the nearest spatial parent in the Node hierarchy
	auto spatialParent = findSpatialParentTransform();
	
	// Update Transform parent using friend access
	m_transform->setParentInternal(spatialParent, keepWorld);
	
	// Propagate dirty state to all spatial children
	propagateTransformDirty();
}

void SpatialNode::propagateTransformDirty()
{
	if (!m_transform)
		return;
	
	m_transform->markDirty();
	
	// Recursively mark all spatial children as dirty
	for (const auto &child : children)
	{
		if (child && child->isSpatial())
		{
			auto spatialChild = std::dynamic_pointer_cast<SpatialNode>(child);
			if (spatialChild)
			{
				spatialChild->propagateTransformDirty();
			}
		}
	}
}

} // namespace engine::scene::nodes
