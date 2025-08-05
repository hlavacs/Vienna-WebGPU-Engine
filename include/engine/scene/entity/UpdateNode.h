#pragma once
#include "engine/scene/entity/Node.h"

namespace engine::scene::entity
{
/**
 * @brief Node with update and lateUpdate methods for per-frame logic.
 */
class UpdateNode : public Node
{
  public:
	using Ptr = std::shared_ptr<UpdateNode>;
	virtual ~UpdateNode() = default;

	/** @brief Called every frame. */
	virtual void update(float deltaTime) {}
	/** @brief Called after all updates. */
	virtual void lateUpdate(float deltaTime) {}
};
} // namespace engine::scene::entity
