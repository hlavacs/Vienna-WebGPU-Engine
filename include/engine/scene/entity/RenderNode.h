#pragma once
#include "engine/scene/entity/Node.h"
#include "engine/rendering/RenderCollector.h"

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
	
	RenderNode() {
		addNodeType(NodeType::Render);
	}
	
	virtual ~RenderNode() = default;

	/** @brief Called before rendering begins. For preparation and state setup. */
	virtual void preRender() {}

	/** @brief Called after rendering completes. For cleanup. */
	virtual void postRender() {}

	/** @brief Collect render data for this node (Option B). */
	virtual void onRenderCollect(engine::rendering::RenderCollector &collector) {}
};
} // namespace engine::scene::entity
