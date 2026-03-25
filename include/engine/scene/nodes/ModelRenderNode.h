#pragma once

#include "engine/rendering/Model.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/scene/nodes/RenderNode.h"
#include "engine/scene/nodes/SpatialNode.h"

namespace engine::resources
{
class ResourceManager;
}

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
		const engine::rendering::Model::Handle &modelHandle,
		uint32_t layer = 0
	) : m_modelHandle(modelHandle), m_renderLayer(layer)
	{
		addNodeType(NodeType::Model);
	}

	/**
	 * @brief Constructs a model render node.
	 * @param model Shared pointer to the model resource to render.
	 * @param layer Render layer for sorting (default: 0).
	 */
	explicit ModelRenderNode(
		const engine::rendering::Model::Ptr model,
		uint32_t layer = 0
	) : m_modelHandle(model->getHandle()), m_renderLayer(layer)
	{
		addNodeType(NodeType::Model);
	}

	/**
	 * @brief Constructs a model render node with lazy loading from file path.
	 * The model will be loaded during initialize() when the scene becomes active.
	 * @param modelPath Path to the model file to load.
	 * @param layer Render layer for sorting (default: 0).
	 */
	explicit ModelRenderNode(
		const std::filesystem::path &modelPath,
		uint32_t layer = 0
	) : m_modelPath(modelPath), m_renderLayer(layer), m_loadFromPath(true)
	{
		addNodeType(NodeType::Model);
	}

	~ModelRenderNode() override = default;

	/**
	 * @brief Initialize the node - marks that loading is needed.
	 */
	void initialize() override
	{
		Node::initialize();
		// Actual loading will be handled by SceneManager
	}

	/**
	 * @brief Check if this node needs its model loaded from path.
	 * @return true if model should be loaded, false otherwise
	 */
	bool needsLoading() const
	{
		return m_loadFromPath && !m_modelPath.empty() && !m_modelHandle.valid();
	}

	/**
	 * @brief Get the model path for loading.
	 * @return The model file path
	 */
	const std::filesystem::path &getModelPath() const { return m_modelPath; }

	/**
	 * @brief Set the loaded model handle (called by SceneManager after loading).
	 * @param handle The loaded model handle
	 */
	void setLoadedModel(const engine::core::Handle<engine::rendering::Model> &handle)
	{
		m_modelHandle = handle;
	}

	/**
	 * @brief Clean up model resources.
	 */
	void onDestroy() override
	{
		// Invalidate the model handle to release reference
		if (m_loadFromPath)
			m_modelHandle = engine::core::Handle<engine::rendering::Model>();

		// Call base class to handle children
		Node::onDestroy();
	}

	/**
	 * @brief Add this model to the render collector.
	 * @param collector The render collector to add items to.
	 */
	void onRenderCollect(engine::rendering::RenderCollector &collector) override
	{
		if (m_modelHandle.valid())
		{
			// Use node ID as object ID for bind group caching
			uint64_t objectID = getId();

			// Add model directly to collector
			collector.addModel(
				m_modelHandle,
				getTransform().getWorldMatrix(),
				m_renderLayer,
				objectID
			);
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

	/** @brief Override to draw transform axes when debug is enabled */
	void onDebugDraw(engine::rendering::DebugRenderCollector &collector) override
	{
		SpatialNode::onDebugDraw(collector);
	}

  private:
	engine::core::Handle<engine::rendering::Model> m_modelHandle;
	uint32_t m_renderLayer = 0;
	std::filesystem::path m_modelPath; // Path for lazy loading
	bool m_loadFromPath = false;	   // Flag to indicate loading from path
};

} // namespace engine::scene::nodes
