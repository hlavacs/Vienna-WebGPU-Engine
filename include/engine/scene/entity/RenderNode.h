#pragma once
#include "engine/scene/entity/Node.h"

namespace engine::scene::entity
{
/**
 * @brief Node with render method for rendering logic.
 */
class RenderNode : public Node
{
  public:
	using Ptr = std::shared_ptr<RenderNode>;
	virtual ~RenderNode() = default;

	/** @brief Called to render the node. */
	virtual void render() {}
};
} // namespace engine::scene::entity
