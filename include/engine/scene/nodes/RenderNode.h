#pragma once
#include "engine/scene/nodes/Node.h"
#include <memory>
#include <vector>

// Forward declare RenderCollector to avoid circular dependency
namespace engine::rendering
{
class RenderCollector;
struct BindGroupDataProvider;
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

	RenderNode(std::optional<std::string> name = std::nullopt) : Node(name)
	{
		addNodeType(NodeType::Render);
	}

	~RenderNode() override = default;

	/**
	 * @brief Called before rendering to allow nodes to provide custom bind group data.
	 * Override this to populate custom uniform data for shaders.
	 * 
	 * @param outProviders Vector to append custom bind group data providers to
	 * 
	 * Example usage:
	 * @code
	 *   void MyNode::preRender(std::vector<BindGroupDataProvider> &outProviders)
	 *   {
	 *       MyCustomUniforms uniforms;
	 *       uniforms.customColor = glm::vec4(1.0f, 0.5f, 0.2f, 1.0f);
	 *       uniforms.customIntensity = std::sin(engine()->time()) * 0.5f + 0.5f;
	 *       
	 *       outProviders.push_back(BindGroupDataProvider::create(
	 *           "MyCustomShader",      // Shader name
	 *           "MyCustomUniforms",    // Bind group name
	 *           uniforms               // Uniform data
	 *       ));
	 *   }
	 * @endcode
	 */
	virtual void preRender(std::vector<engine::rendering::BindGroupDataProvider> &outProviders) {}

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
