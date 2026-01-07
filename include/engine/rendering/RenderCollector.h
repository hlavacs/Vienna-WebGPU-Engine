#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/core/Handle.h"
#include "engine/math/AABB.h"
#include "engine/math/Frustum.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Model.h"

namespace engine::rendering
{

/**
 * @brief CPU-only renderable item collected for rendering.
 * Contains no GPU objects - those are created during renderer preparation.
 * Stores world-space AABB for deferred culling.
 */
struct RenderItemCPU
{
	engine::rendering::Model::Handle modelHandle;
	engine::rendering::Submesh submesh;
	glm::mat4 worldTransform;
	engine::math::AABB worldBounds; // World-space bounding box for culling
	uint32_t renderLayer = 0;
	uint64_t objectID = 0; // Unique object ID for bind group caching

	bool operator<(const RenderItemCPU &other) const
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
 * @brief Collects CPU-side render items and lights from the scene graph.
 *
 * This is a CPU-only collector - it does not create or reference any GPU objects.
 * GPU object creation and bind group management happens in the Renderer during prepareRenderItems().
 *
 * IMPORTANT: addModel() does NOT perform frustum culling. Culling happens on-demand
 * via extractVisible() and extractForLight() query methods.
 */
class RenderCollector
{
  public:
	RenderCollector() = default;

	/**
	 * @brief Adds a model to be rendered with object ID for bind group caching.
	 * Does NOT perform culling - items are collected unconditionally.
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
	 * @param light Light object with type-specific data.
	 */
	void addLight(const Light &light);

	/**
	 * @brief Sorts render items by layer, then by material for batching.
	 */
	void sort();

	/**
	 * @brief Clears all collected items.
	 */
	void clear();

	/**
	 * @brief Extracts items visible from a camera frustum.
	 * @param frustum View frustum for culling.
	 * @return Indices of visible items.
	 */
	std::vector<size_t> extractVisible(const engine::math::Frustum &frustum) const;

	/**
	 * @brief Extracts items visible from a directional/spot light (frustum-based).
	 * @param lightFrustum Light's frustum (orthographic for directional, perspective for spot).
	 * @return Indices of visible items.
	 */
	std::vector<size_t> extractForLightFrustum(const engine::math::Frustum &lightFrustum) const;

	/**
	 * @brief Extracts items visible from a point light (sphere-based).
	 * @param lightPosition Light's world position.
	 * @param lightRange Light's maximum range.
	 * @return Indices of visible items.
	 */
	std::vector<size_t> extractForPointLight(const glm::vec3 &lightPosition, float lightRange) const;

	/**
	 * @brief Assigns shadow indices to lights and extracts their GPU-friendly uniforms.
	 * @param maxShadow2D Maximum number of 2D shadow maps.
	 * @param maxShadowCube Maximum number of cube shadow maps.
	 * @return Vector of LightStruct uniforms with shadow indices assigned.
	 */
	std::vector<LightStruct> extractLightUniformsWithShadows(uint32_t maxShadow2D, uint32_t maxShadowCube) const;

	/**
	 * @brief Gets all collected render items.
	 * @return Const reference to render items vector.
	 */
	const std::vector<RenderItemCPU> &getRenderItems() const { return m_renderItems; };

	/**
	 * @brief Gets all collected lights.
	 * @return Const reference to lights vector.
	 */
	const std::vector<Light> &getLights() const { return m_lights; };

	/**
	 * @brief Extracts light uniforms for GPU rendering.
	 * @return Vector of LightStruct uniforms ready for GPU upload.
	 */
	std::vector<LightStruct> extractLightUniforms() const;

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
	/**
	 * @brief Tests if an AABB is visible in a frustum.
	 */
	static bool isAABBVisibleInFrustum(
		const engine::math::AABB &aabb,
		const engine::math::Frustum &frustum
	);

	/**
	 * @brief Tests if an AABB intersects a sphere.
	 */
	static bool isAABBInSphere(
		const engine::math::AABB &aabb,
		const glm::vec3 &center,
		float radius
	);

	std::vector<RenderItemCPU> m_renderItems;
	std::vector<Light> m_lights;
};

} // namespace engine::rendering
