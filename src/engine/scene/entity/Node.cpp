#include "engine/scene/entity/Node.h"
#include "engine/scene/entity/RenderNode.h"
#include "engine/scene/entity/UpdateNode.h"
#include "engine/scene/entity/PhysicsNode.h"

namespace engine::scene::entity
{
Node::Node() {}
Node::~Node() { onDestroy(); }

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
}

void Node::setEngineContext(engine::EngineContext* context)
{
	m_engineContext = context;
	// Propagate to all children
	for (auto& child : children)
	{
		if (child)
			child->setEngineContext(context);
	}
}

Node *Node::getParent() const { return parent; }
const std::vector<Node::Ptr> &Node::getChildren() const { return children; }

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

} // namespace engine::scene::entity
