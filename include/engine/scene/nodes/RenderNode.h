#pragma once
#include "engine/scene/nodes/Node.h"
#include "engine/rendering/RenderProxies.h"
#include <vector>
#include <memory>

namespace engine::scene::nodes
{
/**
 * @brief Node with preRender, render, and postRender methods for the rendering cycle.
 * Uses virtual inheritance to prevent diamond inheritance issues.
 * 
 * RenderNodes produce RenderProxy objects during scene traversal, completely
 * decoupling the scene graph from the rendering system. The renderer processes
 * these proxies to create GPU resources and submit draw calls.
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

	/**
	 * @brief Collect render proxies for this node.
	 * 
	 * Nodes produce RenderProxy objects (ModelRenderProxy, UIRenderProxy, DebugRenderProxy)
	 * that contain all data needed for rendering, without coupling to GPU/renderer internals.
	 * 
	 * @param outProxies Vector to append RenderProxy objects to.
	 */
	virtual void collectRenderProxies(std::vector<std::shared_ptr<engine::rendering::RenderProxy>> &outProxies) {}
};
} // namespace engine::scene::nodes
