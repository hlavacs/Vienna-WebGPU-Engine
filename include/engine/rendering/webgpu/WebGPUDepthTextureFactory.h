#pragma once
#include <cassert>
#include <iostream>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

/**
 * @class WebGPUDepthTextureFactory
 * @brief Factory for creating WebGPU depth textures and views.
 *
 * Provides a default minimal depth texture creation and fully configurable creation.
 * Does not cache textures; caching should be done by the renderer or a manager.
 */

/**
 * @class WebGPUDepthTextureFactory
 * @brief Factory for creating WebGPU depth textures and views.
 *
 * Provides a default minimal depth texture creation and fully configurable creation.
 * Does not cache textures; caching should be done by the renderer or a manager.
 */
class WebGPUDepthTextureFactory
{
  public:
	/**
	 * @brief Construct the factory with a WebGPU context.
	 * @param context Pointer to the WebGPUContext. Must not be nullptr.
	 */
	explicit WebGPUDepthTextureFactory(WebGPUContext& context);

	/**
	 * @brief Create a default depth texture and view for the given size.
	 * @param width Framebuffer width
	 * @param height Framebuffer height
	 * @param format Depth texture format (default: Depth32Float)
	 * @return WebGPUTexture containing the created texture and view
	 */
	std::shared_ptr<WebGPUTexture> createDefault(uint32_t width, uint32_t height, wgpu::TextureFormat format = wgpu::TextureFormat::Depth32Float);

	/**
	 * @brief Create a fully configurable depth texture and view.
	 * @param width Texture width
	 * @param height Texture height
	 * @param format Texture format
	 * @param mipLevelCount Number of mip levels
	 * @param arrayLayerCount Number of array layers
	 * @param sampleCount Multisample count
	 * @param usage Texture usage flags
	 * @return WebGPUTexture containing the created texture and view
	 */
	std::shared_ptr<WebGPUTexture> create(uint32_t width, uint32_t height, wgpu::TextureFormat format, uint32_t mipLevelCount, uint32_t arrayLayerCount, uint32_t sampleCount, wgpu::TextureUsage usage);

  private:
	/**
	 * @brief Pointer to the WebGPU context used for resource creation.
	 */
	WebGPUContext& m_context;
};

} // namespace engine::rendering::webgpu
