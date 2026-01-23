#pragma once

#include <memory>
#include <utility>

namespace engine::rendering
{

class FrameCache;

namespace webgpu
{
class WebGPUContext;
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
