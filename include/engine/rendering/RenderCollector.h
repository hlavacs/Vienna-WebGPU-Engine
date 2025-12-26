#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "engine/core/Handle.h"
#include "engine/math/Frustum.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Model.h"

namespace engine::rendering
{

/**
 * @brief Single renderable item collected for rendering.
 */
struct RenderItem
{
	engine::rendering::Model::Handle modelHandle;
	engine::rendering::Submesh submesh;
	glm::mat4 worldTransform;
	uint32_t renderLayer = 0;

	bool operator<(const RenderItem &other) const
	{
		if (renderLayer != other.renderLayer)
			return renderLayer < other.renderLayer;

		auto aMatId = submesh.material.id();
		auto bMatId = other.submesh.material.id();
		if (aMatId != bMatId)
			return aMatId < bMatId;

		if (modelHandle.id() != other.modelHandle.id())
			return modelHandle.id() < other.modelHandle.id();

		return submesh.indexOffset < other.submesh.indexOffset;
	}
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
	RenderCollector(
		glm::mat4 viewMatrix,
		glm::mat4 projMatrix,
		glm::vec3 cameraPosition,
		engine::math::Frustum frustum = {}
	) : m_viewMatrix(viewMatrix),
		m_projMatrix(projMatrix),
		m_cameraPosition(cameraPosition),
		m_frustum(frustum)
	{
	}

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
	const std::vector<RenderItem> &getRenderItems() const { return m_renderItems; };

	/**
	 * @brief Gets all collected lights.
	 * @return Const reference to lights vector.
	 */
	const std::vector<LightStruct> &getLights() const { return m_lights; };

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

  private:
	bool isAABBVisible(
		const engine::math::AABB &aabb
	) const;

	std::vector<RenderItem> m_renderItems;
	std::vector<LightStruct> m_lights;

	glm::mat4 m_viewMatrix;
	glm::mat4 m_projMatrix;
	glm::vec3 m_cameraPosition;
	engine::math::Frustum m_frustum;
};

} // namespace engine::rendering
