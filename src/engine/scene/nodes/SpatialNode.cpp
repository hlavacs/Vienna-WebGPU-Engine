#include "engine/scene/nodes/SpatialNode.h"
#include "engine/rendering/DebugCollector.h"

namespace engine::scene::nodes
{
SpatialNode::SpatialNode() : m_transform(std::make_shared<Transform>())
{
	addNodeType(NodeType::Spatial);
}

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

} // namespace engine::scene::nodes
