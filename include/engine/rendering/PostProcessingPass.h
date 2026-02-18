#pragma once

#include "engine/rendering/RenderPass.h"

#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

namespace engine::rendering
{

struct FrameCache;
namespace webgpu
{
class WebGPUContext;
class WebGPUBindGroup;
class WebGPUPipeline;
class WebGPUTexture;
struct WebGPURenderPassContext;
} // namespace webgpu

/**
 * @class PostProcessingPass
 * @brief Applies screen-space post-processing effects to rendered images.
 *
 * This pass performs post-processing on the final rendered scene by sampling
 * from the input texture and applying various effects (vignette, tone mapping,
 * color grading, etc.). Renders a fullscreen triangle without vertex buffers.
 *
 * @note This pass should be executed after the main mesh and debug rendering
 *       passes but before compositing to the surface.
 *
 * Example usage:
 * @code
 * auto postPass = std::make_unique<PostProcessingPass>(context);
 * postPass->initialize();
 * postPass->setRenderPassContext(renderPassContext);
 * postPass->setCameraId(cameraId);
 * postPass->render(frameCache);
 * @endcode
 */
class PostProcessingPass : public RenderPass
{
  public:
	/**
	 * @brief Construct a post-processing pass.
	 * @param context The WebGPU context for resource creation.
	 */
	explicit PostProcessingPass(std::shared_ptr<webgpu::WebGPUContext> context);

	/**
	 * @brief Initialize the pass (get shader, create pipeline, sampler).
	 * @return True if initialization succeeded, false otherwise.
	 */
	bool initialize() override;

	/**
	 * @brief Render the post-processing effects for the current camera.
	 * @param frameCache Frame-wide data including render targets.
	 *
	 * Samples from the camera's render target texture, applies vignette effect,
	 * and renders to the output specified by the render pass context.
	 */
	void render(FrameCache &frameCache) override;

	/**
	 * @brief Clean up GPU resources (bind group cache).
	 */
	void cleanup() override;

	/**
	 * @brief Set which camera's render target to post-process.
	 * @param cameraId Camera identifier for looking up the render target.
	 */
	void setCameraId(uint64_t cameraId)
	{
		m_cameraId = cameraId;
	}

	/**
	 * @brief Set the render pass context for this pass.
	 * @param context Render pass context containing target textures and descriptors.
	 */
	void setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext> &context);

	/**
	 * @brief Set the input texture for post-processing.
	 * @param texture The texture to sample from in the post-processing shader.
	 */
	void setInputTexture(const std::shared_ptr<webgpu::WebGPUTexture> &texture);

  private:
	/**
	 * @brief Get or create the render pipeline for this pass.
	 * Checks if the pipeline already exists and is valid; if not, creates a new pipeline
	 * using the shader info and render pass context. Caches the pipeline for future use.
	 * @return Shared pointer to the render pipeline.
	 */
	std::shared_ptr<webgpu::WebGPUPipeline> getOrCreatePipeline();

	/**
	 * @brief Record and submit GPU commands for the post-processing pass.
	 * 
	 * Handles the complete command recording lifecycle:
	 * - Creates command encoder
	 * - Begins render pass
	 * - Records drawing commands
	 * - Submits to GPU queue
	 * 
	 * @param pipeline The render pipeline to use.
	 * @param bindGroup The bind group containing texture and sampler resources.
	 */
	void recordAndSubmitCommands(
		const std::shared_ptr<webgpu::WebGPUPipeline> &pipeline,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
	);

	/**
	 * @brief Get or create a bind group for the given texture.
	 * @param texture The texture to create a bind group for.
	 * @return Cached or newly created bind group.
	 */
	std::shared_ptr<webgpu::WebGPUBindGroup> getOrCreateBindGroup(
		const std::shared_ptr<webgpu::WebGPUTexture> &texture
	);

	// Camera ID to determine which render target to sample from in the frame cache
	uint64_t m_cameraId = 0;

	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shaderInfo;
	wgpu::Sampler m_sampler = nullptr;
	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;
	std::shared_ptr<webgpu::WebGPUTexture> m_inputTexture;
	std::weak_ptr<webgpu::WebGPUPipeline> m_pipeline;

	// Bind group cache by texture pointer
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_bindGroupCache;
};

} // namespace engine::rendering
