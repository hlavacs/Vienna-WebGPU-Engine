#pragma once

#include "engine/scene/nodes/SpatialNode.h"

namespace engine::scene::nodes
{
/**
 * @brief Spatial node with fixedUpdate method for physics logic.
 */
class PhysicsNode : public SpatialNode
{
  public:
	using Ptr = std::shared_ptr<PhysicsNode>;

	PhysicsNode()
	{
		addNodeType(NodeType::Physics);
	}

	~PhysicsNode() override = default;

	/** @brief Called at fixed intervals for physics updates. */
	virtual void fixedUpdate(float fixedDeltaTime) {}
};

} // namespace engine::scene::nodes
