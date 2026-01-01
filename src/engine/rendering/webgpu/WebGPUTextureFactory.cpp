#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUTextureFactory::WebGPUTextureFactory(WebGPUContext &context) :
	BaseWebGPUFactory(context) {}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createFromColor(
	const glm::vec3 &color,
	uint32_t width,
	uint32_t height
)
{
	// Check cache first
	auto it = m_colorTextureCache.find(std::make_tuple(static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f), static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f), static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f), 255, width, height));
	if (it != m_colorTextureCache.end())
	{
		return it->second;
	}
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
	desc.format = wgpu::TextureFormat::RGBA8UnormSrgb;
	desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	desc.mipLevelCount = 1;
	desc.sampleCount = 1;
	desc.viewFormatCount = 0;
	desc.viewFormats = nullptr;

	wgpu::Texture gpuTexture = m_context.getDevice().createTexture(desc);

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

	wgpu::TextureView view = gpuTexture.createView(viewDesc);
	auto texturePtr = std::make_shared<WebGPUTexture>(
		gpuTexture,
		view,
		desc,
		viewDesc
	);
	m_colorTextureCache[std::make_tuple(r, g, b, a, width, height)] = texturePtr;
	return texturePtr;
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createFromHandleUncached(
	const TextureHandle &textureHandle,
	const WebGPUTextureOptions &options
)
{
	auto textureOpt = textureHandle.get();
	if (!textureOpt || !textureOpt.value())
	{
		spdlog::error("WebGPUTextureFactory::createFromHandle: Invalid texture handle {}", textureHandle.id());
		return nullptr;
	}

	const auto &texture = *textureOpt.value();

	if (texture.getType() == Texture::Type::Surface)
	{
		spdlog::error("Surface textures cannot be created via createFromHandleUncached");
		return nullptr;
	}

	std::string textureName = texture.getName().value_or(std::to_string(textureHandle.id()));

	// Format
	wgpu::TextureFormat format = options.format.value_or(wgpu::TextureFormat::Undefined);
	if (format == wgpu::TextureFormat::Undefined)
	{
		switch (texture.getType())
		{
		case Texture::Type::Image:
			format = WebGPUTexture::mapImageFormatToGPU(texture.getImage()->getFormat());
			break;
		case Texture::Type::RenderTarget:
			format = wgpu::TextureFormat::RGBA8UnormSrgb;
			break;
		case Texture::Type::DepthStencil:
			format = wgpu::TextureFormat::Depth24PlusStencil8;
			break;
		case Texture::Type::Depth:
			format = wgpu::TextureFormat::Depth24Plus;
			break;
		}
	}

	// Mip levels
	uint32_t mipLevelCount = options.generateMipmaps
								 ? 1 + static_cast<uint32_t>(std::floor(std::log2(std::max(texture.getWidth(), texture.getHeight()))))
								 : 1;

	// Usage
	wgpu::TextureUsage usage = options.usage.value_or(wgpu::TextureUsage::None);
	if (usage == wgpu::TextureUsage::None)
	{
		switch (texture.getType())
		{
		case Texture::Type::Image:
			usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);
			break;
		case Texture::Type::RenderTarget:
			usage = static_cast<WGPUTextureUsage>(
				WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc
			);
			break;
		case Texture::Type::DepthStencil:
		case Texture::Type::Depth:
			usage = WGPUTextureUsage_RenderAttachment;
			break;
		}
	}

	// Descriptor
	wgpu::TextureDescriptor desc{};
	desc.label = ("Texture_" + textureName).c_str();
	desc.dimension = wgpu::TextureDimension::_2D;
	desc.size.width = texture.getWidth();
	desc.size.height = texture.getHeight();
	desc.size.depthOrArrayLayers = 1;
	desc.format = format;
	desc.mipLevelCount = mipLevelCount;
	desc.sampleCount = 1;
	desc.usage = usage;

	wgpu::Texture gpuTexture = m_context.getDevice().createTexture(desc);

	// Upload base level for images
	if (texture.getType() == Texture::Type::Image)
		uploadTextureData(texture, gpuTexture);

	// Generate mipmaps
	if (options.generateMipmaps && mipLevelCount > 1)
		generateMipmaps(gpuTexture, format, texture.getWidth(), texture.getHeight(), mipLevelCount);

	// Create default view
	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.format = desc.format;
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = mipLevelCount;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = wgpu::TextureAspect::All;

	wgpu::TextureView textureView = gpuTexture.createView(viewDesc);

	return std::make_shared<WebGPUTexture>(
		gpuTexture,
		textureView,
		desc,
		viewDesc,
		texture.getType(),
		textureOpt.value()
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

	wgpu::Texture gpuTexture = m_context.getDevice().createTexture(textureDesc);
	wgpu::TextureView view = gpuTexture.createView(viewDesc);
	return std::make_shared<WebGPUTexture>(
		gpuTexture,
		view,
		textureDesc,
		viewDesc
	);
}

void WebGPUTextureFactory::uploadTextureData(const Texture &texture, wgpu::Texture &gpuTexture)
{
	wgpu::ImageCopyTexture dst{};
	dst.texture = gpuTexture;
	dst.mipLevel = 0;
	dst.origin = {0, 0, 0};
	dst.aspect = wgpu::TextureAspect::All;

	wgpu::TextureDataLayout layout{};
	layout.offset = 0;
	layout.bytesPerRow = texture.getWidth() * texture.getChannels();
	layout.rowsPerImage = texture.getHeight();

	wgpu::Extent3D extent{texture.getWidth(), texture.getHeight(), 1};

	m_context.getQueue().writeTexture(dst, texture.getPixels().data(), texture.getWidth() * texture.getHeight() * texture.getChannels(), layout, extent);
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::getWhiteTexture()
{
	if (!m_whiteTexture)
	{
		m_whiteTexture = createFromColor(glm::vec3(1.0f), 1, 1);
	}
	return m_whiteTexture;
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::getBlackTexture()
{
	if (!m_blackTexture)
	{
		m_blackTexture = createFromColor(glm::vec3(0.0f), 1, 1);
	}
	return m_blackTexture;
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
