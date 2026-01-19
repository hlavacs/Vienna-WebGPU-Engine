#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

#include <fstream>
#include <spdlog/spdlog.h>

#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"

namespace engine::rendering::webgpu
{

WebGPUTextureFactory::WebGPUTextureFactory(WebGPUContext &context) :
	BaseWebGPUFactory(context)
{
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createFromColor(
	const glm::vec3 &color,
	uint32_t width,
	uint32_t height,
	ColorSpace colorSpace
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

	// Choose format based on color space
	wgpu::TextureFormat format = (colorSpace == ColorSpace::sRGB)
									 ? wgpu::TextureFormat::RGBA8UnormSrgb
									 : wgpu::TextureFormat::RGBA8Unorm;

	wgpu::TextureDescriptor desc{};
	desc.label = ("ColorTexture (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + std::to_string(a) + ")").c_str();
	desc.dimension = wgpu::TextureDimension::_2D;
	desc.size.width = width;
	desc.size.height = height;
	desc.size.depthOrArrayLayers = 1;
	desc.format = format;
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
	auto texturePtr = std::shared_ptr<WebGPUTexture>(
		new WebGPUTexture(
			gpuTexture,
			view,
			desc,
			viewDesc
		)
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

	// Determine color space
	ColorSpace colorSpace = options.colorSpace.value_or(ColorSpace::sRGB);

	// Format
	wgpu::TextureFormat format = options.format.value_or(wgpu::TextureFormat::Undefined);
	if (format == wgpu::TextureFormat::Undefined)
	{
		switch (texture.getType())
		{
		case Texture::Type::Image:
			format = WebGPUTexture::mapImageFormatToGPU(texture.getImage()->getFormat(), colorSpace);
			break;
		case Texture::Type::RenderTarget:
			format = wgpu::TextureFormat::RGBA8UnormSrgb;
			break;
		case Texture::Type::DepthStencil:
			format = wgpu::TextureFormat::Depth32FloatStencil8;
			break;
		case Texture::Type::Depth:
			format = wgpu::TextureFormat::Depth32Float;
			break;
		}
	}
	else
	{
		// Apply color space to the provided format
		format = WebGPUTexture::applyColorSpaceToFormat(format, colorSpace);
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
			// If mipmaps will be generated, we need RenderAttachment usage
			// because each mip level is rendered to during generation
			if (options.generateMipmaps && mipLevelCount > 1)
			{
				usage = static_cast<WGPUTextureUsage>(
					WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment
				);
			}
			else
			{
				usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);
			}
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

	return std::shared_ptr<WebGPUTexture>(
		new WebGPUTexture(
			gpuTexture,
			textureView,
			desc,
			viewDesc,
			texture.getType(),
			textureOpt.value()
		)
	);
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createRenderTarget(
	uint32_t renderTargetId,
	uint32_t width,
	uint32_t height,
	wgpu::TextureFormat format
)
{
	// ToDo: Thread safety
	auto it = m_renderTargetCache.find(renderTargetId);
	if (it != m_renderTargetCache.end())
	{
		if (it->second->getWidth() == width && it->second->getHeight() == height && it->second->getTextureDescriptor().format == format)
			return it->second;
		spdlog::debug("Resizing cached render target texture {}x{} format {}", width, height, static_cast<int>(format));
	}
	else
	{
		spdlog::debug("Creating new render target texture {}x{} format {}", width, height, static_cast<int>(format));
	}

	// Create new render target texture
	wgpu::TextureDescriptor textureDesc{};
	textureDesc.label = ("RenderTarget_" + std::to_string(width) + "x" + std::to_string(height)).c_str();
	textureDesc.dimension = wgpu::TextureDimension::_2D;
	textureDesc.size.width = width;
	textureDesc.size.height = height;
	textureDesc.size.depthOrArrayLayers = 1;
	textureDesc.format = format;
	textureDesc.usage = static_cast<WGPUTextureUsage>(
		WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc
	);
	textureDesc.mipLevelCount = 1;
	textureDesc.sampleCount = 1;

	wgpu::Texture gpuTexture = m_context.getDevice().createTexture(textureDesc);

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.format = textureDesc.format;
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = wgpu::TextureAspect::All;

	wgpu::TextureView view = gpuTexture.createView(viewDesc);

	auto texturePtr = std::shared_ptr<WebGPUTexture>(
		new WebGPUTexture(
			gpuTexture,
			view,
			textureDesc,
			viewDesc,
			Texture::Type::RenderTarget,
			nullptr
		)
	);

	// Cache for reuse
	m_renderTargetCache[renderTargetId] = texturePtr;

	return texturePtr;
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

	wgpu::Texture gpuTexture = m_context.getDevice().createTexture(textureDesc);
	wgpu::TextureView view = gpuTexture.createView(viewDesc);
	return std::shared_ptr<WebGPUTexture>(
		new WebGPUTexture(
			gpuTexture,
			view,
			textureDesc,
			viewDesc
		)
	);
}

void WebGPUTextureFactory::uploadTextureData(const Texture &texture, wgpu::Texture &gpuTexture)
{
	if (!texture.getImage())
	{
		spdlog::warn("Texture has no image data to upload");
		return;
	}

	auto image = texture.getImage();
	const void *pixelData = nullptr;
	size_t dataSize = 0;

	// Get pixel data based on format
	if (image->isLDR())
	{
		const auto &pixels = image->getPixels8();
		pixelData = pixels.data();
		dataSize = pixels.size();
	}
	else
	{
		const auto &pixels = image->getPixelsF();
		pixelData = pixels.data();
		dataSize = pixels.size() * sizeof(float);
	}

	wgpu::ImageCopyTexture dst{};
	dst.texture = gpuTexture;
	dst.mipLevel = 0;
	dst.origin = {0, 0, 0};
	dst.aspect = wgpu::TextureAspect::All;

	wgpu::TextureDataLayout layout{};
	layout.offset = 0;
	layout.bytesPerRow = texture.getWidth() * texture.getChannels() * (image->getFormat() >= engine::resources::ImageFormat::Type::HDR_RGBA16F ? sizeof(float) : 1);
	layout.rowsPerImage = texture.getHeight();

	wgpu::Extent3D extent{texture.getWidth(), texture.getHeight(), 1};

	m_context.getQueue().writeTexture(dst, pixelData, dataSize, layout, extent);
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

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createShadowMap2DArray(
	uint32_t size,
	uint32_t arrayLayers
)
{
	wgpu::TextureDescriptor textureDesc{};
	textureDesc.label = "Shadow Maps 2D Array";
	textureDesc.size = {size, size, arrayLayers};
	textureDesc.mipLevelCount = 1;
	textureDesc.sampleCount = 1;
	textureDesc.dimension = wgpu::TextureDimension::_2D;
	textureDesc.format = wgpu::TextureFormat::Depth32Float;
	textureDesc.usage = static_cast<WGPUTextureUsage>(
		WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
	);

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label = "Shadow Maps 2D Array View";
	viewDesc.format = wgpu::TextureFormat::Depth32Float;
	viewDesc.dimension = wgpu::TextureViewDimension::_2DArray;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = arrayLayers;
	viewDesc.aspect = wgpu::TextureAspect::DepthOnly;

	return createFromDescriptors(textureDesc, viewDesc);
}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createShadowMapCubeArray(
	uint32_t size,
	uint32_t numCubes
)
{
	wgpu::TextureDescriptor textureDesc{};
	textureDesc.label = "Shadow Maps Cube Array";
	textureDesc.size = {size, size, 6 * numCubes};
	textureDesc.mipLevelCount = 1;
	textureDesc.sampleCount = 1;
	textureDesc.dimension = wgpu::TextureDimension::_2D;
	textureDesc.format = wgpu::TextureFormat::Depth32Float;
	textureDesc.usage = static_cast<WGPUTextureUsage>(
		WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
	);

	// DEFAULT VIEW: 2D ARRAY
	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label = "Shadow Maps Cube Array (2DArray View)";
	viewDesc.format = wgpu::TextureFormat::Depth32Float;
	viewDesc.dimension = wgpu::TextureViewDimension::CubeArray;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = 6 * numCubes;
	viewDesc.aspect = wgpu::TextureAspect::DepthOnly;

	return createFromDescriptors(textureDesc, viewDesc);
}

void WebGPUTextureFactory::generateMipmaps(
	wgpu::Texture gpuTexture,
	wgpu::TextureFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t mipLevelCount
)
{
	if (!gpuTexture)
	{
		spdlog::error("Cannot generate mipmaps for invalid texture");
		return;
	}

	if (mipLevelCount <= 1)
	{
		spdlog::warn("Texture has only 1 mip level, no mipmaps to generate");
		return;
	}

	// Get or create mipmap pipeline for this format
	auto mipmapPipeline = getOrCreateMipmapPipeline(format);
	if (!mipmapPipeline || !mipmapPipeline->isValid())
	{
		spdlog::error("Failed to get/create mipmap pipeline for format {}", static_cast<int>(format));
		return;
	}

	// Get shader info to access bind group layout
	auto mipmapShader = m_context.shaderRegistry().getShader(shader::default ::MIPMAP_BLIT);
	if (!mipmapShader || !mipmapShader->isValid())
	{
		spdlog::error("Failed to get mipmap shader for bind group layout");
		return;
	}

	auto bindGroupLayouts = mipmapShader->getBindGroupLayoutVector();
	if (bindGroupLayouts.empty())
	{
		spdlog::error("Mipmap shader has no bind group layouts");
		return;
	}

	// Get mipmap sampler from factory
	auto mipmapSampler = m_context.samplerFactory().getMipmapSampler();

	wgpu::CommandEncoder encoder = m_context.getDevice().createCommandEncoder();

	// Generate mipmaps by repeatedly blitting with linear filtering
	for (uint32_t mipLevel = 1; mipLevel < mipLevelCount; ++mipLevel)
	{
		uint32_t srcWidth = std::max(1u, width >> (mipLevel - 1));
		uint32_t srcHeight = std::max(1u, height >> (mipLevel - 1));
		uint32_t dstWidth = std::max(1u, width >> mipLevel);
		uint32_t dstHeight = std::max(1u, height >> mipLevel);

		// Create views for source and destination mip levels
		wgpu::TextureViewDescriptor srcViewDesc{};
		srcViewDesc.baseMipLevel = mipLevel - 1;
		srcViewDesc.mipLevelCount = 1;
		srcViewDesc.baseArrayLayer = 0;
		srcViewDesc.arrayLayerCount = 1;
		srcViewDesc.aspect = wgpu::TextureAspect::All;
		wgpu::TextureView srcView = gpuTexture.createView(srcViewDesc);

		wgpu::TextureViewDescriptor dstViewDesc{};
		dstViewDesc.baseMipLevel = mipLevel;
		dstViewDesc.mipLevelCount = 1;
		dstViewDesc.baseArrayLayer = 0;
		dstViewDesc.arrayLayerCount = 1;
		dstViewDesc.aspect = wgpu::TextureAspect::All;
		wgpu::TextureView dstView = gpuTexture.createView(dstViewDesc);

		// Create bind group for this mip level
		std::vector<wgpu::BindGroupEntry> entries(2);
		entries[0].binding = 0;
		entries[0].textureView = srcView;
		entries[1].binding = 1;
		entries[1].sampler = mipmapSampler;

		wgpu::BindGroupDescriptor bindGroupDesc{};
		bindGroupDesc.layout = bindGroupLayouts[0]->getLayout();
		bindGroupDesc.entryCount = entries.size();
		bindGroupDesc.entries = entries.data();
		wgpu::BindGroup bindGroup = m_context.getDevice().createBindGroup(bindGroupDesc);

		// Set up render pass to blit from src to dst
		wgpu::RenderPassColorAttachment colorAttachment{};
		colorAttachment.view = dstView;
		colorAttachment.loadOp = wgpu::LoadOp::Clear;
		colorAttachment.storeOp = wgpu::StoreOp::Store;
		colorAttachment.clearValue = {0.0, 0.0, 0.0, 0.0};

		wgpu::RenderPassDescriptor renderPassDesc{};
		renderPassDesc.colorAttachmentCount = 1;
		renderPassDesc.colorAttachments = &colorAttachment;

		wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
		renderPass.setPipeline(mipmapPipeline->getPipeline());
		renderPass.setBindGroup(0, bindGroup, 0, nullptr);
		renderPass.draw(3, 1, 0, 0); // Fullscreen triangle
		renderPass.end();

		bindGroup.release();
		srcView.release();
		dstView.release();
	}

	wgpu::CommandBuffer commands = encoder.finish();
	m_context.getQueue().submit(commands);
	commands.release();
	encoder.release();

	spdlog::debug("Generated {} mipmap levels for texture", mipLevelCount - 1);
}

std::shared_ptr<WebGPUPipeline> WebGPUTextureFactory::getOrCreateMipmapPipeline(wgpu::TextureFormat format)
{
	// Get the mipmap blit shader from registry
	auto mipmapShader = m_context.shaderRegistry().getShader(shader::default ::MIPMAP_BLIT);
	if (!mipmapShader || !mipmapShader->isValid())
	{
		spdlog::error("Failed to get mipmap blit shader from registry");
		return nullptr;
	}

	// Create render pipeline using the pipeline manager with the specific format
	auto mipmapPipeline = m_context.pipelineManager().getOrCreatePipeline(
		mipmapShader,					// shader
		format,							// color format (specific to this texture)
		wgpu::TextureFormat::Undefined, // no depth
		engine::rendering::Topology::Type::Triangles,
		wgpu::CullMode::None,
		1 // sample count
	);

	if (!mipmapPipeline || !mipmapPipeline->getPipeline())
	{
		spdlog::error("Failed to create mipmap pipeline for format {}", static_cast<int>(format));
		return nullptr;
	}
	spdlog::debug("Created mipmap generation pipeline for format {}", static_cast<int>(format));

	return mipmapPipeline;
}

} // namespace engine::rendering::webgpu
