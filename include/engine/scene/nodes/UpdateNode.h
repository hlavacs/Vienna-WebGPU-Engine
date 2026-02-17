#pragma once
#include "engine/scene/nodes/Node.h"

namespace engine::scene::nodes
{
/**
 * @brief Node with update and lateUpdate methods for per-frame logic.
 * Uses virtual inheritance to prevent diamond inheritance issues.
 */
class UpdateNode : public virtual Node
{
  public:
	using Ptr = std::shared_ptr<UpdateNode>;

	UpdateNode()
	{ 
		addNodeType(NodeType::Update);
	}

	~UpdateNode() override = default;

	/** @brief Called every frame. */
	virtual void update([[maybe_unused]]float deltaTime) {}
	/** @brief Called after all updates. */
	virtual void lateUpdate([[maybe_unused]]float deltaTime) {}
};
} // namespace engine::scene::nodes
