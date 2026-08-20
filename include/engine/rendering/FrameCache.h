#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/rendering/Light.h"
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/RenderTarget.h"
#include "engine/rendering/ShadowRequest.h"

// Forward declarations
namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUBindGroup;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

// Forward declarations
class RenderCollector;
struct BindGroupDataProvider;

/**
 * @brief Frame-wide rendering data cache.
 *
 * Centralizes rendering data for a single frame:
 * - CPU-side scene data (lights, render items)
 * - GPU-ready uniform data
 * - Lazy GPU resource preparation and caching
 * - Custom bind group management
 *
 * Caches:
 * - frameBindGroupCache: Frame bind groups per camera (key: cameraId)
 * - objectBindGroupCache: Object bind groups per object (key: objectId)
 * - customBindGroupCache: Custom user bind groups (key: "ShaderName:BindGroupName[:InstanceId]")
 *
 * Lifecycle:
 * @code
 *   frameCache.clearForNewFrame();
 *   frameCache.prepareGPUResources(context, collector, indices);
 *   frameCache.processBindGroupProviders(context, providers);
 * @endcode
 */
struct FrameCache
{
	std::vector<Light> lights;																	 ///< CPU-side light objects
	std::vector<LightStruct> lightUniforms;														 ///< GPU-ready light uniform data
	std::vector<ShadowRequest> shadowRequests;													 ///< Shadow requests for this frame
	std::vector<ShadowUniform> shadowUniforms;													 ///< GPU-ready shadow uniform data
	std::unordered_map<uint64_t, RenderTarget> renderTargets;									 ///< Render targets for all cameras this frame
	std::vector<std::optional<RenderItemGPU>> gpuRenderItems;									 ///< Lazy-prepared GPU resources
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> frameBindGroupCache;	 ///< Per-frame bind group cache
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> objectBindGroupCache; ///< Per-object bind group cache
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUTexture>> finalTextures;			 ///< Cache of final rendered textures per camera (key: cameraId) for compositing pass

	/**
	 * @brief Cache for custom user-defined bind groups.
	 * Key format:
	 *   - Shared (instanceId=nullopt): "ShaderName:BindGroupName" (for Global/PerFrame reuse)
	 *   - Per-instance (instanceId=value): "ShaderName:BindGroupName:InstanceId" (for PerObject/PerMaterial reuse)
	 *
	 * The BindGroupReuse policy from the shader's bind group layout determines caching behavior:
	 *   - Global/PerFrame: instanceId should be nullopt (shared across all objects)
	 *   - PerObject/PerMaterial: instanceId should be provided (unique per object/material)
	 *
	 * Custom bind groups are automatically created on first access via processBindGroupProviders()
	 * and cached for the duration of the frame.
	 *
	 * Example (shared - Global/PerFrame):
	 *   outProviders.push_back(BindGroupDataProvider::create(
	 *       "MyShader", "MyCustomData", myUniforms, BindGroupReuse::PerFrame));
	 *
	 * Example (per-instance - PerObject/PerMaterial):
	 *   uint64_t objectId = reinterpret_cast<uint64_t>(this);
	 *   outProviders.push_back(BindGroupDataProvider::create(
	 *       "MyShader", "MyCustomData", myUniforms, BindGroupReuse::PerObject, objectId));
	 *
	 * Renderer automatically calls processBindGroupProviders() which creates/updates the bind group.
	 */
	std::unordered_map<std::string, std::shared_ptr<webgpu::WebGPUBindGroup>> customBindGroupCache;

	float time = 0.0f; ///< Current frame time

	/**
	 * @brief Creates a cache key for custom bind groups.
	 * Key format:
	 *   - Shared (no instanceId): "ShaderName:BindGroupName"
	 *   - Per-instance (with instanceId): "ShaderName:BindGroupName:InstanceId"
	 *
	 * @param shaderName Name of the shader
	 * @param bindGroupName Name of the bind group
	 * @param instanceId Optional instance ID for per-object/material bind groups
	 * @return Cache key string
	 */
	static std::string createCustomBindGroupCacheKey(
		const std::string &shaderName,
		const std::string &bindGroupName,
		std::optional<uint64_t> instanceId = std::nullopt
	)
	{
		std::string cacheKey;
		cacheKey.reserve(shaderName.size() + bindGroupName.size() + 20);
		cacheKey = shaderName;
		cacheKey += ':';
		cacheKey += bindGroupName;

		if (instanceId.has_value())
		{
			cacheKey += ':';
			cacheKey += std::to_string(instanceId.value());
		}

		return cacheKey;
	}

	/**
	 * @brief Processes bind group data providers from nodes.
	 * Creates/updates bind groups based on provided data.
	 *
	 * @param context WebGPU context for resource creation
	 * @param providers Data providers from RenderNode::preRender()
	 * @return True if all bind groups were processed successfully
	 */
	bool processBindGroupProviders(
		std::shared_ptr<webgpu::WebGPUContext> context,
		const std::vector<BindGroupDataProvider> &providers
	);

	/**
	 * @brief Prepares GPU resources for the specified indices from the collector.
	 *
	 * This method creates GPU resources (models, meshes, materials, bind groups)
	 * from CPU-side data in the RenderCollector. Resources are cached in
	 * gpuRenderItems and reused if already prepared.
	 *
	 * @param context WebGPU context for resource creation.
	 * @param collector The render collector with CPU-side data.
	 * @param indices Indices of items to prepare.
	 * @return True if all resources were prepared successfully.
	 */
	bool prepareGPUResources(
		std::shared_ptr<webgpu::WebGPUContext> context,
		const RenderCollector &collector,
		const std::vector<size_t> &indices
	);

	/**
	 * @brief End-of-frame reset. Cheap — drops only the truly per-frame data
	 *        that's rebuilt fresh next frame (lights, shadow requests, render
	 *        targets, final textures, time). Preserves the four GPU resource
	 *        caches so the next frame can short-circuit re-preparation:
	 *
	 *  - `gpuRenderItems` — per-visible-item GPU bundle. Persisted so
	 *    `prepareGPUResources` can skip the expensive factory+bind-group
	 *    rebuild for objects that were already prepared on a previous
	 *    frame; `prepareGPUResources` then walks just the objectID/
	 *    transform delta to decide whether to refresh the uniform buffer.
	 *  - `customBindGroupCache` — keyed by `shader:bindGroupName[:instanceId]`.
	 *    `processBindGroupProviders` was designed to hit this cache and
	 *    `updateBuffer` instead of `createBindGroup` every frame; clearing
	 *    it per-frame defeats that. Persisting it across frames means new
	 *    custom bind groups are created exactly once.
	 *  - `frameBindGroupCache`, `objectBindGroupCache` — already persisted
	 *    today.
	 *
	 * Use `clear()` (below) when the scene actually changes — that's the
	 * only context where dropping the GPU caches is correct.
	 */
	void endFrame()
	{
		lights.clear();
		lightUniforms.clear();
		shadowRequests.clear();
		shadowUniforms.clear();
		renderTargets.clear();
		finalTextures.clear();
		time = 0.0f;
	}

	/**
	 * @brief Full reset — drops every cache, including the GPU resource
	 *        bundles that `endFrame()` preserves. Use on scene change /
	 *        Clear All. Per-frame paths should call `endFrame()` instead;
	 *        invoking `clear()` per frame defeats every dirty-tracking
	 *        optimization in `prepareGPUResources` /
	 *        `processBindGroupProviders`.
	 *
	 * Does NOT release GPU resources directly — they're managed via
	 * `shared_ptr`. Dropping the cache entries just means external owners
	 * (render passes, etc.) are the only thing keeping them alive.
	 */
	void clear()
	{
		endFrame();
		gpuRenderItems.clear();
		customBindGroupCache.clear();
		frameBindGroupCache.clear();
		objectBindGroupCache.clear();
	}
};

} // namespace engine::rendering
