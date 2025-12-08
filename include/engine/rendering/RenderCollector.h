#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "engine/core/Handle.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Model.h"

namespace engine::rendering
{

/**
 * @brief Item to be rendered with its world transform.
 */
struct RenderItem
{
	engine::core::Handle<engine::rendering::Model> model;
	glm::mat4 worldTransform;
	uint32_t renderLayer = 0; // For sorting/filtering
};

/**
 * @brief Collects render items and lights from the scene graph.
 *
 * Used during scene traversal to gather all renderable objects
 * and lights for submission to the renderer.
 */
class RenderCollector
{
  public:
	RenderCollector() = default;

	/**
	 * @brief Adds a model to be rendered.
	 * @param model Handle to the model resource.
	 * @param transform World-space transform matrix.
	 * @param layer Render layer for sorting (default: 0).
	 */
	void addModel(
		const engine::core::Handle<engine::rendering::Model> &model,
		const glm::mat4 &transform,
		uint32_t layer = 0
	);

	/**
	 * @brief Adds a light to the scene.
	 * @param light Light data structure.
	 */
	void addLight(const LightStruct &light);
	
	/**
	 * @brief Adds a debug transform for visualization (axes rendering).
	 * @param transform World-space transform matrix.
	 */
	void addDebugTransform(const glm::mat4 &transform);

	/**
	 * @brief Sorts render items by layer, then by material for batching.
	 */
	void sort();

	/**
	 * @brief Clears all collected items.
	 */
	void clear();

	/**
	 * @brief Gets all collected render items.
	 * @return Const reference to render items vector.
	 */
	const std::vector<RenderItem> &getRenderItems() const;

	/**
	 * @brief Gets all collected lights.
	 * @return Const reference to lights vector.
	 */
	const std::vector<LightStruct> &getLights() const;

	/**
	 * @brief Gets the number of collected render items.
	 * @return Item count.
	 */
	size_t getRenderItemCount() const { return m_renderItems.size(); }

	/**
	 * @brief Gets the number of collected lights.
	 * @return Light count.
	 */
	size_t getLightCount() const { return m_lights.size(); }
	
	/**
	 * @brief Gets all collected debug transforms.
	 * @return Const reference to debug transforms vector.
	 */
	const std::vector<glm::mat4> &getDebugTransforms() const { return m_debugTransforms; }
	
	/**
	 * @brief Gets the number of collected debug transforms.
	 * @return Debug transform count.
	 */
	size_t getDebugTransformCount() const { return m_debugTransforms.size(); }

  private:
	std::vector<RenderItem> m_renderItems;
	std::vector<LightStruct> m_lights;
	std::vector<glm::mat4> m_debugTransforms;
};

} // namespace engine::rendering
