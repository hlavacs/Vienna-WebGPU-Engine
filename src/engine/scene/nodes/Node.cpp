#include "engine/scene/nodes/Node.h"

#include "engine/EngineContext.h"
#include "engine/rendering/DebugRenderCollector.h"
#include "engine/scene/nodes/PhysicsNode.h"
#include "engine/scene/nodes/RenderNode.h"
#include "engine/scene/nodes/SpatialNode.h"
#include "engine/scene/nodes/UpdateNode.h"

namespace engine::scene::nodes
{

void Node::start()
{
	if (!enabled || started)
		return;
	started = true;
	for (auto &child : children)
	{
		if (child && child->isEnabled())
			child->start();
	}
}

void Node::onEnable()
{
	for (auto &child : children)
	{
		if (child && child->isEnabled())
			child->onEnable();
	}
}

void Node::onDisable()
{
	for (auto &child : children)
	{
		if (child && child->isEnabled())
			child->onDisable();
	}
}

void Node::onDestroy()
{
	for (auto &child : children)
	{
		if (child)
			child->onDestroy();
	}
	children.clear();
}

void Node::onDebugDraw(engine::rendering::DebugRenderCollector &collector)
{
	// Default implementation: do nothing
	// Derived classes can override to add debug visualization
}

engine::resources::ResourceManager *Node::getResourceManager() const
{
	return m_engineContext ? m_engineContext->getResourceManager() : nullptr;
}

const engine::resources::ResourceManager *Node::resourceManager() const
{
	return m_engineContext ? m_engineContext->getResourceManager() : nullptr;
}

void Node::enable()
{
	if (!enabled)
	{
		enabled = true;
		if (!started)
			start();
		onEnable();
	}
}

void Node::disable()
{
	if (enabled)
	{
		enabled = false;
		onDisable();
	}
}

bool Node::isEnabled() const { return enabled; }

void Node::addChild(Ptr child)
{
	if (!child)
		return;
	child->parent = this;
	child->setEngineContext(m_engineContext); // Propagate context to child
	children.push_back(child);

	// Update Transform hierarchy if child is spatial
	if (child->isSpatial())
	{
		auto spatialChild = std::dynamic_pointer_cast<SpatialNode>(child);
		if (spatialChild)
		{
			spatialChild->updateTransformParent(true); // Keep world transform
		}
	}

	if (enabled && child->isEnabled())
		child->start();
}

void Node::removeChild(Ptr child)
{
	if (!child)
		return;
	children.erase(std::remove(children.begin(), children.end(), child), children.end());
	child->parent = nullptr;
	child->setEngineContext(nullptr); // Clear context when removed

	// Clear Transform parent if child is spatial
	if (child->isSpatial())
	{
		auto spatialChild = std::dynamic_pointer_cast<SpatialNode>(child);
		if (spatialChild)
		{
			spatialChild->updateTransformParent(true); // Keep world transform
		}
	}
}

void Node::setEngineContext(engine::EngineContext *context)
{
	m_engineContext = context;
	// Propagate to all children
	for (auto &child : children)
	{
		if (child)
			child->setEngineContext(context);
	}
}

std::vector<Node::Ptr> Node::getChildren(std::optional<std::string> name) const
{
	if (!name.has_value())
	{
		return children;
	}
	std::vector<Node::Ptr> filteredChildren;
	for (const auto &child : children)
	{
		if (child && child->getName().has_value() && child->getName().value() == name)
		{
			filteredChildren.push_back(child);
		}
	}
	return filteredChildren;
}

std::shared_ptr<RenderNode> Node::asRenderNode()
{
	if (isRender())
	{
		return std::dynamic_pointer_cast<RenderNode>(shared_from_this());
	}
	return nullptr;
}

std::shared_ptr<UpdateNode> Node::asUpdateNode()
{
	if (isUpdate())
	{
		return std::dynamic_pointer_cast<UpdateNode>(shared_from_this());
	}
	return nullptr;
}

std::shared_ptr<PhysicsNode> Node::asPhysicsNode()
{
	if (isPhysics())
	{
		return std::dynamic_pointer_cast<PhysicsNode>(shared_from_this());
	}
	return nullptr;
}

std::shared_ptr<SpatialNode> Node::asSpatialNode()
{
	if (isSpatial())
	{
		return std::dynamic_pointer_cast<SpatialNode>(shared_from_this());
	}
	return nullptr;
}
} // namespace engine::scene::nodes
