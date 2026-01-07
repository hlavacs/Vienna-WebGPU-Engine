#pragma once

#include <memory>
#include <unordered_map>

#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
	class WebGPUContext;
	class WebGPUTexture;
	class WebGPUPipeline;
	class WebGPUBindGroup;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

/**
 * @brief CompositePass handles final compositing of offscreen textures to the surface.
 * 
 * Uses the fullscreen quad shader from ShaderRegistry.
 * Simple pass that just renders textures to surface with no depth testing.
 */
class CompositePass
{
public:
	explicit CompositePass(std::shared_ptr<webgpu::WebGPUContext> context);

	/**
	 * @brief Initialize the composite pass.
	 * @return True if initialization succeeded.
	 */
	bool initialize();

	/**
	 * @brief Render a texture to the surface using a fullscreen quad.
	 * @param renderPassContext The render pass context for the surface.
	 * @param texture The texture to composite.
	 */
	void render(
		const std::shared_ptr<webgpu::WebGPURenderPassContext>& renderPassContext,
		const std::shared_ptr<webgpu::WebGPUTexture>& texture
	);

	/**
	 * @brief Clear cached bind groups.
	 */
	void clearCache();

private:
	std::shared_ptr<webgpu::WebGPUContext> m_context;
	std::shared_ptr<webgpu::WebGPUPipeline> m_pipeline;
	wgpu::Sampler m_sampler = nullptr;
	
	// Bind group cache by texture pointer
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_bindGroupCache;
};

} // namespace engine::rendering
