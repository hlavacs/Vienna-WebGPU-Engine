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
	// Find the nearest spatial parent in the Node hierarchy and rebind. This
	// marks our transform dirty; descendants do not need an eager dirty sweep -
	// each one rebuilds lazily the next time its world matrix is read, because
	// getWorldMatrix() compares against the parent's world version, which our
	// rebuild bumps. Walking the subtree here would also wrongly bump every
	// descendant's local version (Versioned) without a local change.
	auto spatialParent = findSpatialParentTransform();
	m_transform.setParentInternal(spatialParent, keepWorld);
}

} // namespace engine::scene::nodes
