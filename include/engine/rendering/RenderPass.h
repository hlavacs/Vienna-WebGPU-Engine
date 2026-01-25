#pragma once

#include <memory>
#include <utility>

#include <webgpu/webgpu.hpp>

namespace engine::rendering
{

class FrameCache; // Forward declaration

namespace webgpu
{
class WebGPUContext; // Forward declaration
class WebGPUBindGroup; // Forward declaration
class WebGPUShaderInfo; // Forward declaration
}

/**
 * @brief Base class for all render passes.
 *
 * Provides a common interface for render passes with:
 * - Initialization and cleanup
 * - Rendering with FrameCache (core rendering data)
 * - WebGPU context access
 *
 * Passes should receive additional dependencies (like RenderCollector) via setters,
 * not as render() parameters.
 */
class RenderPass
{
  public:
	virtual ~RenderPass() = default;

	/**
	 * @brief Initialize the render pass.
	 * Creates GPU resources like pipelines, bind groups, textures, etc.
	 * @return true if initialization succeeded, false otherwise.
	 */
	virtual bool initialize() = 0;

	/**
	 * @brief Render using the frame cache.
	 * @param frameCache The frame cache containing all render data for this frame.
	 */
	virtual void render(FrameCache &frameCache) = 0;

	/**
	 * @brief Bind a bind group to the render pass based on shader info using the shader's bind group layout.
	 * @param renderPass The render pass encoder.
	 * @param webgpuShaderInfo The shader info containing information about bind groups.
	 * @param bindgroup The bind group to bind.
	 * @return true if binding succeeded, false otherwise.
	 */
	static bool bind(
		wgpu::RenderPassEncoder renderPass,
		const std::shared_ptr<webgpu::WebGPUShaderInfo> &webgpuShaderInfo,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &bindgroup
	);

	/**
	 * @brief Clean up GPU resources.
	 */
	virtual void cleanup() = 0;

  protected:
	/**
	 * @brief Constructor for derived classes.
	 * @param context Shared WebGPU context for device, queue, and factory access.
	 */
	explicit RenderPass(std::shared_ptr<webgpu::WebGPUContext> context) :
		m_context(std::move(context))
	{
	}

	std::shared_ptr<webgpu::WebGPUContext> m_context;
};

} // namespace engine::rendering
