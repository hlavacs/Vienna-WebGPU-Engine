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
	virtual ~PhysicsNode() = default;

	/** @brief Called at fixed intervals for physics. */
	virtual void fixedUpdate(float fixedDeltaTime) {}
};
} // namespace engine::scene::entity
