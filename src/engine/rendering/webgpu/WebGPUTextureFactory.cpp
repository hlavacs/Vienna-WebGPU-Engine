#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUTextureFactory::WebGPUTextureFactory(WebGPUContext &context) :
	BaseWebGPUFactory(context) {}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createFromHandleUncached(
	const engine::rendering::TextureHandle &textureHandle
)
{
	auto textureOpt = textureHandle.get();
	if (!textureOpt || !textureOpt.value())
	{
		throw std::runtime_error("Invalid texture handle in WebGPUTextureFactory::createFromHandle");
	}
	const auto &texture = *textureOpt.value();
	std::string textureName = texture.getName().value_or(std::to_string(textureHandle.id()));
	// Prepare texture descriptor
	wgpu::TextureDescriptor desc{};
	desc.label = ("Texture_" + textureName).c_str();
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

	return std::make_shared<WebGPUTexture>(
		gpuTexture,
		nextTexture,
		desc,
		viewDesc
	);
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createFromColor(
	const glm::vec3 &color,
	uint32_t width,
	uint32_t height
)
{
	// Create a width x height RGBA8 texture with the given color
	std::vector<uint8_t> rgba(width * height * 4);
	uint8_t r = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
	uint8_t g = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
	uint8_t b = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
	uint8_t a = 255;
	for (uint32_t i = 0; i < width * height; ++i)
	{
		rgba[i * 4 + 0] = r;
		rgba[i * 4 + 1] = g;
		rgba[i * 4 + 2] = b;
		rgba[i * 4 + 3] = a;
	}
	wgpu::TextureDescriptor desc{};
	desc.label = ("ColorTexture (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + std::to_string(a) + ")").c_str();
	desc.dimension = wgpu::TextureDimension::_2D;
	desc.size.width = width;
	desc.size.height = height;
	desc.size.depthOrArrayLayers = 1;
	desc.format = wgpu::TextureFormat::RGBA8Unorm;
	desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	desc.mipLevelCount = 1;
	desc.sampleCount = 1;
	desc.viewFormatCount = 0;
	desc.viewFormats = nullptr;

	wgpu::Texture gpuTexture = m_context.createTexture(desc);

	wgpu::ImageCopyTexture dst{};
	dst.texture = gpuTexture;
	dst.mipLevel = 0;
	dst.origin = {0, 0, 0};
	dst.aspect = wgpu::TextureAspect::All;

	wgpu::TextureDataLayout layout{};
	layout.offset = 0;
	layout.bytesPerRow = width * 4;
	layout.rowsPerImage = height;

	wgpu::Extent3D extent{width, height, 1};
	m_context.getQueue().writeTexture(dst, rgba.data(), rgba.size(), layout, extent);

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.format = desc.format;
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = wgpu::TextureAspect::All;

	auto texture = engine::rendering::Texture(width, height, 4, std::move(rgba));
	wgpu::TextureView view = gpuTexture.createView(viewDesc);
	return std::make_shared<WebGPUTexture>(
		gpuTexture,
		view,
		desc,
		viewDesc
	);
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createFromDescriptors(
	const wgpu::TextureDescriptor &textureDesc,
	const wgpu::TextureViewDescriptor &viewDesc
)
{
	// Assert compatibility between descriptors
	assert(textureDesc.format == viewDesc.format && "Texture and view formats must match");
	assert(viewDesc.baseMipLevel + viewDesc.mipLevelCount <= textureDesc.mipLevelCount && "View mip levels must be within texture mip levels");
	assert(viewDesc.baseArrayLayer + viewDesc.arrayLayerCount <= textureDesc.size.depthOrArrayLayers && "View array layers must be within texture array layers");
	// Optionally check dimension compatibility
	assert(viewDesc.dimension == wgpu::TextureViewDimension::_2D && textureDesc.dimension == wgpu::TextureDimension::_2D && "Only 2D textures/views supported");

	wgpu::Texture gpuTexture = m_context.createTexture(textureDesc);
	wgpu::TextureView view = gpuTexture.createView(viewDesc);
	return std::make_shared<WebGPUTexture>(
		gpuTexture,
		view,
		textureDesc,
		viewDesc
	);
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::getWhiteTexture()
{
	if (!m_whiteTexture)
	{
		m_whiteTexture = createFromColor(glm::vec3(1.0f), 1, 1);
	}
	return m_whiteTexture;
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::getDefaultNormalTexture()
{
	if (!m_defaultNormalTexture)
	{
		// Default normal is (0.5, 0.5, 1.0) in tangent space
		m_defaultNormalTexture = createFromColor(glm::vec3(0.5f, 0.5f, 1.0f), 1, 1);
	}
	return m_defaultNormalTexture;
}

} // namespace engine::rendering::webgpu
