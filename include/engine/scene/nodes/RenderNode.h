#pragma once
#include "engine/scene/nodes/Node.h"
#include <vector>
#include <memory>

// Forward declare RenderCollector to avoid circular dependency
namespace engine::rendering
{
class RenderCollector;
}

namespace engine::scene::nodes
{
/**
 * @brief Node with preRender, render, and postRender methods for the rendering cycle.
 * Uses virtual inheritance to prevent diamond inheritance issues.
 * 
 * RenderNodes add themselves to the RenderCollector during scene traversal.
 */
class RenderNode : public virtual Node
{
  public:
	using Ptr = std::shared_ptr<RenderNode>;
	
	RenderNode(std::optional<std::string> name = std::nullopt) : Node(name){
		addNodeType(NodeType::Render);
	}
	
	virtual ~RenderNode() = default;

	/** @brief Called before rendering begins. For preparation and state setup. */
	virtual void preRender() {}

	/** @brief Called after rendering completes. For cleanup. */
	virtual void postRender() {}

	/**
	 * @brief Add this node's renderable data to the collector.
	 * 
	 * @param collector The render collector to add items to.
	 */
	virtual void onRenderCollect(engine::rendering::RenderCollector &collector) {}
};
} // namespace engine::scene::nodes
