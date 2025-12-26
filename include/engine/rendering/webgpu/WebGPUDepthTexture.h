
#pragma once
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include <memory>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

class WebGPUContext;

/**
 * @class WebGPUDepthTexture
 * @brief Specialized WebGPUTexture for depth buffers, with additional depth-specific methods.
 */
class WebGPUDepthTexture : public WebGPUTexture
{
  public:
	WebGPUDepthTexture(
		wgpu::Texture texture,
		wgpu::TextureView textureView,
		const wgpu::TextureDescriptor &textureDesc,
		const wgpu::TextureViewDescriptor &viewDesc
	) : WebGPUTexture(texture, textureView, textureDesc, viewDesc)
	{
		m_isDepthTexture = true;
	}
};

} // namespace engine::rendering::webgpu