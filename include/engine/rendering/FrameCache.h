#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "engine/rendering/Light.h"
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/RenderTarget.h"

// Forward declarations
namespace engine::rendering::webgpu
{
class WebGPUContext;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

// Forward declarations
class RenderCollector;

/**
 * @brief Frame cache for rendering-wide data.
 * Contains data that applies to the entire current frame and should be
 * cleared/reset at the start and end of each frame.
 * 
 * Centralizes all GPU-ready data used by rendering passes:
 * - CPU-side Light objects
 * - GPU-ready LightStruct and ShadowUniform arrays
 * - Prepared GPU render items (models, meshes, materials)
 * - Render targets for all cameras this frame
 * 
 * The FrameCache follows a lazy preparation pattern:
 * - GPU resources are prepared on-demand via prepareGPUResources()
 * - Resources are cached and reused within the same frame
 * - Cache is cleared at frame boundaries
 */
struct FrameCache
{
	std::vector<Light> lights;                              ///< CPU-side light objects
	std::vector<LightStruct> lightUniforms;                 ///< GPU-ready light uniform data
	std::vector<ShadowUniform> shadowUniforms;              ///< GPU-ready shadow uniform data
	std::vector<RenderTarget> renderTargets;                ///< Render targets for all cameras this frame
	std::vector<std::optional<RenderItemGPU>> gpuRenderItems; ///< Lazy-prepared GPU resources
	float time = 0.0f;                                      ///< Current frame time

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
	 * @brief Clears all frame cache data.
	 * Call at the end of each frame to reset for the next frame.
	 */
	void clear()
	{
		lights.clear();
		lightUniforms.clear();
		shadowUniforms.clear();
		renderTargets.clear();
		gpuRenderItems.clear();
		time = 0.0f;
	}
};

} // namespace engine::rendering
