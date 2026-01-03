#pragma once

#include "engine/rendering/Model.h"
#include "engine/scene/nodes/RenderNode.h"
#include "engine/scene/nodes/SpatialNode.h"

namespace engine::scene::nodes
{

/**
 * @brief A node that renders a 3D model.
 *
 * Combines RenderNode functionality with model rendering.
 * Automatically adds its model to the RenderCollector during scene traversal.
 * Inherits from SpatialNode to have a transform for positioning the model.
 */
class ModelRenderNode : public RenderNode, public SpatialNode
{
  public:
	using Ptr = std::shared_ptr<ModelRenderNode>;

	/**
	 * @brief Constructs a model render node.
	 * @param modelHandle Handle to the model resource to render.
	 * @param layer Render layer for sorting (default: 0).
	 */
	explicit ModelRenderNode(
		const engine::core::Handle<engine::rendering::Model> &modelHandle,
		uint32_t layer = 0
	) : m_modelHandle(modelHandle), m_renderLayer(layer)
	{
		addNodeType(NodeType::Model);
	}

	virtual ~ModelRenderNode() = default;

	/**
	 * @brief Collects render proxies for this model.
	 * @param outProxies Vector to append RenderProxy objects to.
	 */
	void collectRenderProxies(std::vector<std::shared_ptr<engine::rendering::RenderProxy>> &outProxies) override
	{
		if (m_modelHandle.valid() && getTransform())
		{
			// Use node ID as object ID for bind group caching
			uint64_t objectID = getId();

			// Create a ModelRenderProxy with this model's data
			auto proxy = std::make_shared<engine::rendering::ModelRenderProxy>(
				m_modelHandle,
				getTransform()->getWorldMatrix(),
				m_renderLayer,
				engine::core::Handle<engine::rendering::Material>(), // No material override
				objectID
			);
			outProxies.push_back(proxy);
		}
	}

	/**
	 * @brief Sets the model handle.
	 * @param modelHandle The new model handle.
	 */
	void setModel(const engine::core::Handle<engine::rendering::Model> &modelHandle)
	{
		m_modelHandle = modelHandle;
	}

	/**
	 * @brief Gets the model handle.
	 * @return The current model handle.
	 */
	const engine::core::Handle<engine::rendering::Model> &getModel() const
	{
		return m_modelHandle;
	}

	/**
	 * @brief Sets the render layer.
	 * @param layer The new render layer.
	 */
	void setRenderLayer(uint32_t layer)
	{
		m_renderLayer = layer;
	}

	/**
	 * @brief Gets the render layer.
	 * @return The current render layer.
	 */
	uint32_t getRenderLayer() const
	{
		return m_renderLayer;
	}

  private:
	engine::core::Handle<engine::rendering::Model> m_modelHandle;
	uint32_t m_renderLayer = 0;
};

} // namespace engine::scene::nodes
