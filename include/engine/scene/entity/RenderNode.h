#pragma once
#include "engine/scene/entity/Node.h"

namespace engine::scene::entity
{
/**
 * @brief Node with preRender, render, and postRender methods for the rendering cycle.
 * Uses virtual inheritance to prevent diamond inheritance issues.
 */
class RenderNode : public virtual Node
{
  public:
	using Ptr = std::shared_ptr<RenderNode>;
	virtual ~RenderNode() = default;

	/** @brief Called before rendering begins. For preparation and state setup. */
	virtual void preRender() {}
	
	/** @brief Called during the rendering phase. For actual draw calls. */
	virtual void render() {}
	
	/** @brief Called after rendering completes. For cleanup. */
	virtual void postRender() {}
};
} // namespace engine::scene::entity
