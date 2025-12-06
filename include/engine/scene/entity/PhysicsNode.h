#pragma once
#include "engine/scene/entity/Node.h"

namespace engine::scene::entity
{
/**
 * @brief Node with fixedUpdate method for physics logic.
 */
class PhysicsNode : public Node
{
  public:
	using Ptr = std::shared_ptr<PhysicsNode>;
	
	PhysicsNode() {
		addNodeType(NodeType::Physics);
	}
	
	virtual ~PhysicsNode() = default;

	/** @brief Called at fixed intervals for physics. */
	virtual void fixedUpdate(float fixedDeltaTime) {}
};
} // namespace engine::scene::entity
