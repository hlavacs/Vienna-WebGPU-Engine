#include "engine/rendering/webgpu/WebGPUDepthTextureFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUDepthTextureFactory::WebGPUDepthTextureFactory(WebGPUContext &context) : m_context(context)
{
}

std::shared_ptr<WebGPUTexture> WebGPUDepthTextureFactory::createDefault(uint32_t width, uint32_t height, wgpu::TextureFormat format)
{
	return create(width, height, format, 1, 1, 1, wgpu::TextureUsage::RenderAttachment);
}

std::shared_ptr<WebGPUTexture> WebGPUDepthTextureFactory::create(
	uint32_t width,
	uint32_t height,
	wgpu::TextureFormat format,
	uint32_t mipLevelCount,
	uint32_t arrayLayerCount,
	uint32_t sampleCount,
	wgpu::TextureUsage usage
)
{
	// Describe the texture
	wgpu::TextureDescriptor desc{};
	desc.label = "DepthTexture";
	desc.dimension = wgpu::TextureDimension::_2D;
	desc.size.width = width;
	desc.size.height = height;
	desc.size.depthOrArrayLayers = arrayLayerCount;
	desc.format = format;
	desc.mipLevelCount = mipLevelCount;
	desc.sampleCount = sampleCount;
	desc.usage = usage;

	// Create the texture
	wgpu::Texture texture = m_context.getDevice().createTexture(desc);
	assert(texture);

	// Describe the view
	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.format = format;
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
	viewDesc.aspect = wgpu::TextureAspect::DepthOnly;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = mipLevelCount;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = arrayLayerCount;

	// Create the view
	wgpu::TextureView view = texture.createView(viewDesc);
	assert(view);

	Texture::Type type;
	if (format == wgpu::TextureFormat::Depth24Plus || format == wgpu::TextureFormat::Depth32Float || format == wgpu::TextureFormat::Depth16Unorm)
	{
		type = Texture::Type::Depth;
	}
	else if (format == wgpu::TextureFormat::Depth24PlusStencil8 || format == wgpu::TextureFormat::Depth32FloatStencil8)
	{
		type = Texture::Type::DepthStencil;
	}
	else
	{
		spdlog::error("WebGPUDepthTextureFactory: Unsupported format for depth texture");
		return nullptr;
	}

	return std::make_shared<WebGPUTexture>(
		texture,
		view,
		desc,
		viewDesc,
		type
	);
}

} // namespace engine::rendering::webgpu
