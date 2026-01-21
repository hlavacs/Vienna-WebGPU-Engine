#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderTarget.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

/**
 * @brief CompositePass handles final compositing of offscreen textures to the surface.
 *
 * Uses the fullscreen quad shader from ShaderRegistry.
 * Simple pass that just renders textures to surface with no depth testing.
 */
class CompositePass : public RenderPass
{
  public:
	explicit CompositePass(std::shared_ptr<webgpu::WebGPUContext> context);

	/**
	 * @brief Initialize the composite pass.
	 * @return True if initialization succeeded.
	 */
	bool initialize() override;

	/**
	 * @brief Set the render pass context for surface rendering.
	 * @param context Render pass context targeting the surface.
	 */
	void setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext> &context)
	{
		m_renderPassContext = context;
	}

	/**
	 * @brief Render textures to the surface using a fullscreen quad.
	 * Composites all render targets in frameCache.renderTargets to the surface.
	 * @param frameCache The frame cache containing render targets.
	 */
	void render(FrameCache &frameCache) override;

	/**
	 * @brief Clear cached bind groups.
	 */
	void cleanup() override;

  private:
	/**
	 * @brief Get or create a bind group for the given texture.
	 * @param texture The texture to create a bind group for.
	 * @return Cached or newly created bind group.
	 */
	std::shared_ptr<webgpu::WebGPUBindGroup> getOrCreateBindGroup(
		const std::shared_ptr<webgpu::WebGPUTexture> &texture
	);

	std::shared_ptr<webgpu::WebGPUPipeline> m_pipeline;
	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shaderInfo;
	wgpu::Sampler m_sampler = nullptr;

	// External dependencies (set via setters)
	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;

	// Bind group cache by texture pointer
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_bindGroupCache;
};

} // namespace engine::rendering
