#include "engine/scene/entity/Node.h"

namespace engine::scene::entity
{
Node::Node() = default;
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
}

Node *Node::getParent() const { return parent; }
const std::vector<Node::Ptr> &Node::getChildren() const { return children; }
} // namespace engine::scene::entity
