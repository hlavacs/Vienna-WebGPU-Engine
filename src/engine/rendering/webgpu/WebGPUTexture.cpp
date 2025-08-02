#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu
{

	WebGPUTexture::WebGPUTexture(
		WebGPUContext &context,
		const engine::rendering::Texture::Handle &textureHandle,
		wgpu::Texture texture,
		wgpu::TextureView textureView,
		uint32_t width,
		uint32_t height,
		wgpu::TextureFormat format,
		WebGPUTextureOptions options)
		: WebGPURenderObject<engine::rendering::Texture>(context, textureHandle, Type::Texture),
		  m_texture(texture),
		  m_textureView(textureView),
		  m_width(width),
		  m_height(height),
		  m_format(format),
		  m_options(std::move(options)) {}

	void WebGPUTexture::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
	{
		// Textures typically don't render themselves directly
		// This method would be used for debug visualization or special effects
		// For now, it is a placeholder
	}

	void WebGPUTexture::updateGPUResources()
	{
		// This would be called when the CPU texture changes
		// Here you would update the GPU texture data
		// For example:
		
		try {
			const auto& texture = getCPUObject();
			
			// Example of what might go here:
			// 1. Check if texture data has changed
			// 2. Update texture data or recreate texture
			
			// In a real implementation, you might ask the factory to update the texture:
			// auto [newTexture, newTextureView] = 
			//     m_context.textureFactory().recreateTexture(getCPUHandle(), m_options);
			// m_texture = newTexture;
			// m_textureView = newTextureView;
		}
		catch (const std::exception& e) {
			// Log error or handle invalid texture
		}
	}

} // namespace engine::rendering::webgpu
