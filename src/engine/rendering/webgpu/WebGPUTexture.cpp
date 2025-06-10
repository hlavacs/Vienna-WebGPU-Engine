#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu
{

	WebGPUTexture::WebGPUTexture(wgpu::Texture texture, wgpu::TextureView view, uint32_t width, uint32_t height, wgpu::TextureFormat format)
		: m_texture(texture), m_view(view), m_width(width), m_height(height), m_format(format)
	{
	}

	wgpu::Texture WebGPUTexture::getTexture() const { return m_texture; }
	wgpu::TextureView WebGPUTexture::getTextureView() const { return m_view; }
	uint32_t WebGPUTexture::getWidth() const { return m_width; }
	uint32_t WebGPUTexture::getHeight() const { return m_height; }
	wgpu::TextureFormat WebGPUTexture::getFormat() const { return m_format; }

} // namespace engine::rendering::webgpu
