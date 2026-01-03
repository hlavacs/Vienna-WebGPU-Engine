#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/core/Handle.h"
#include "engine/math/Frustum.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Model.h"

namespace engine::rendering::webgpu
{
class WebGPUBindGroup;
struct WebGPUBindGroupLayoutInfo;
} // namespace engine::rendering::webgpu

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
	std::shared_ptr<webgpu::WebGPUBindGroup> objectBindGroup; // Bind group for object uniforms (group 2)

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
	RenderCollector() = default;
	RenderCollector(
		glm::mat4 viewMatrix,
		glm::mat4 projMatrix,
		glm::vec3 cameraPosition,
		engine::math::Frustum frustum = {},
		webgpu::WebGPUContext *context = nullptr
	) : m_viewMatrix(viewMatrix),
		m_projMatrix(projMatrix),
		m_cameraPosition(cameraPosition),
		m_frustum(frustum),
		m_context(context)
	{
	}

	/**
	 * @brief Adds a model to be rendered with object ID for bind group caching.
	 * @param model Handle to the model resource.
	 * @param transform World-space transform matrix.
	 * @param layer Render layer for sorting.
	 * @param objectID Unique ID for bind group caching (e.g., node ID).
	 */
	void addModel(
		const engine::core::Handle<engine::rendering::Model> &model,
		const glm::mat4 &transform,
		uint32_t layer,
		uint64_t objectID
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
	 * @brief Updates camera-dependent data for a new frame.
	 * @param viewMatrix New view matrix.
	 * @param projMatrix New projection matrix.
	 * @param cameraPosition New camera position.
	 * @param frustum New view frustum.
	 */
	void updateCameraData(
		const glm::mat4 &viewMatrix,
		const glm::mat4 &projMatrix,
		const glm::vec3 &cameraPosition,
		const engine::math::Frustum &frustum
	);

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

	/**
	 * @brief Sets the object bind group layout for creating per-instance bind groups.
	 * @param layout The bind group layout for object uniforms (group 2).
	 */
	void setObjectBindGroupLayout(std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> layout)
	{
		m_objectBindGroupLayout = layout;
	}

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

	webgpu::WebGPUContext *m_context;
	// Cache object bind groups by a unique key (modelHandle.id() + transform hash)
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_objectBindGroupCache;
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_objectBindGroupLayout;
};

} // namespace engine::rendering
