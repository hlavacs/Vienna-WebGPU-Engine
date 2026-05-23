#pragma once

#include <memory>
#include <webgpu/webgpu.hpp>

namespace engine::rendering
{
struct FrameCache;
class ShadowPass;

namespace webgpu
{
class GBuffer;
class WebGPUContext;
class WebGPUBindGroup;
class WebGPUPipeline;
} // namespace webgpu

/**
 * @class CompositionPass
 * @brief Final composition pass for deferred rendering.
 *
 * Reads G-buffer textures and cluster grid to perform full-screen PBR lighting.
 * Samples lights from the 3D cluster grid to efficiently apply only relevant lights
 * to each pixel, enabling dynamic lighting with 512+ lights on modern hardware.
 *
 * **Input:**
 * - G-buffer (4 textures): Position, Normal, Albedo, Material
 * - Cluster grid (storage buffer): Pre-computed light assignments
 * - Scene light data (uniforms + storage buffer)
 * - Frame uniforms (camera matrices)
 *
 * **Output:**
 * - Final composited image (tone-mapped + gamma-corrected)
 *
 * **Workflow:**
 * 1. Render full-screen quad
 * 2. For each pixel:
 *    a. Sample G-buffer data
 *    b. Look up cluster index from depth
 *    c. Fetch light indices from cluster grid
 *    d. Apply PBR for each affecting light
 *    e. Tone-map (Reinhard) and gamma-correct output
 */
class CompositionPass
{
  public:
	explicit CompositionPass(std::shared_ptr<webgpu::WebGPUContext> context);
	~CompositionPass() = default;

	CompositionPass(const CompositionPass &) = delete;
	CompositionPass &operator=(const CompositionPass &) = delete;

	/**
	 * @brief Initialize the composition pass.
	 * Creates render pipeline and full-screen mesh.
	 *
	 * @return True if initialization succeeded, false otherwise.
	 */
	bool initialize();

	/**
	 * @brief Render the final composited image.
	 *
	 * @param frameCache Frame data with G-buffers and render target
	 * @param gBuffer G-buffer textures (position, normal, albedo, material)
	 * @param sceneLightBindGroup Light data bind group from scene
	 * @param clusterBindGroup Cluster grid bind group
	 * @return True if rendering succeeded, false otherwise.
	 */
	bool render(
		FrameCache &frameCache,
		const std::shared_ptr<webgpu::GBuffer> &gBuffer,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &sceneLightBindGroup,
		const ShadowPass &shadowPass,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &clusterBindGroup
	);

  private:
	std::shared_ptr<webgpu::WebGPUContext> m_context;
	wgpu::RenderPipeline m_pipeline = nullptr;

	// Full-screen quad vertex buffer
	wgpu::Buffer m_fullScreenQuadBuffer = nullptr;
	uint32_t m_fullScreenQuadVertexCount = 0;
};

} // namespace engine::rendering
