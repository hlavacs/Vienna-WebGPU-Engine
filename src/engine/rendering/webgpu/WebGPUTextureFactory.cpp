#include "engine/rendering/webgpu/WebGPUTextureFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/Texture.h"

namespace engine::rendering::webgpu
{

	WebGPUTextureFactory::WebGPUTextureFactory(WebGPUContext &context)
		: BaseWebGPUFactory(context) {}

	std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createFrom(const engine::rendering::Texture &texture)
	{
		// Prepare texture descriptor
		wgpu::TextureDescriptor desc{};
		desc.dimension = wgpu::TextureDimension::_2D;
		desc.size.width = texture.getWidth();
		desc.size.height = texture.getHeight();
		desc.size.depthOrArrayLayers = 1;
		desc.format = wgpu::TextureFormat::RGBA8Unorm; // by convention for bmp, png and jpg file. Be careful with other formats.
		desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
		desc.mipLevelCount = texture.getMipLevelCount();
		desc.sampleCount = 1;
		desc.viewFormatCount = 0;
		desc.viewFormats = nullptr;

		wgpu::Texture gpuTexture = m_context.createTexture(desc);

		// Upload data (assume RGBA8)
		wgpu::ImageCopyTexture dst{};
		dst.texture = gpuTexture;
		dst.mipLevel = 0;
		dst.origin = {0, 0, 0};
		dst.aspect = wgpu::TextureAspect::All;

		wgpu::TextureDataLayout layout{};
		layout.offset = 0;
		layout.bytesPerRow = texture.getWidth() * 4;
		layout.rowsPerImage = texture.getHeight();

		wgpu::Extent3D extent{texture.getWidth(), texture.getHeight(), 1};

		m_context.getQueue().writeTexture(dst, texture.getPixels().data(), texture.getWidth() * texture.getHeight() * 4, layout, extent);

		// Create view
		wgpu::TextureViewDescriptor viewDesc{};
		// Use the same format as the texture
		viewDesc.format = desc.format;
		viewDesc.dimension = wgpu::TextureViewDimension::_2D;
		viewDesc.baseMipLevel = 0;
		viewDesc.mipLevelCount = 1;
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect = wgpu::TextureAspect::All;
		
		wgpu::TextureView nextTexture = gpuTexture.createView(viewDesc);

		return std::make_shared<WebGPUTexture>(gpuTexture, nextTexture, texture.getWidth(), texture.getHeight(), desc.format);
	}

} // namespace engine::rendering::webgpu
