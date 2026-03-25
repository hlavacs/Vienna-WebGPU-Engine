#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include <memory>
#include <spdlog/spdlog.h>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

WebGPUTexture::~WebGPUTexture()
{
	// Release cached layer views
	for (auto &[layer, view] : m_layerViews)
	{
		if (view)
		{
			view.release();
		}
	}
	m_layerViews.clear();

	// Release main view and texture
	if (m_textureView)
	{
		m_textureView.release();
	}
	if (m_texture)
	{
		m_texture.release();
	}
}

bool WebGPUTexture::resize(WebGPUContext &context, uint32_t newWidth, uint32_t newHeight)
{
	if (newWidth == 0 || newHeight == 0)
		return false;
	// No resize needed
	if (matches(newWidth, newHeight, getFormat()))
		return true;

	if (isSurfaceTexture())
	{
		return false;
	}

	// Prepare new descriptors
	wgpu::TextureDescriptor newTexDesc = m_textureDesc;
	newTexDesc.size.width = newWidth;
	newTexDesc.size.height = newHeight;
	if (newTexDesc.format == wgpu::TextureFormat::Undefined)
	{
		return false;
	}

	// Create new texture
	wgpu::Texture newTexture = context.getDevice().createTexture(newTexDesc);
	if (!newTexture)
	{
		return false;
	}

	// Prepare view descriptor
	wgpu::TextureViewDescriptor newViewDesc = m_viewDesc;
	newViewDesc.format = newTexDesc.format;

	// Create new view
	wgpu::TextureView newView = newTexture.createView(newViewDesc);
	if (!newView)
	{
		spdlog::error("[WebGPUTexture] Failed to create texture view.");
		return false;
	}

	// Everything succeeded, release old resources and update members
	// Release cached layer views
	for (auto &[layer, view] : m_layerViews)
	{
		if (view)
		{
			view.release();
		}
	}
	m_layerViews.clear();

	if (m_textureView)
		m_textureView.release();
	if (m_texture)
		m_texture.release();

	m_texture = newTexture;
	m_textureView = newView;
	m_textureDesc = newTexDesc;
	m_viewDesc = newViewDesc;

	if (!m_texture || !m_textureView)
	{
		return false;
	}

	return true;
}

wgpu::TextureView WebGPUTexture::getTextureView(int layer) const
{
	if (layer == -1)
	{
		return m_textureView;
	}
	else if (is2DArrayLayerView())
	{
		return get2DArrayLayerView(static_cast<uint32_t>(layer), "2D Array Layer View");
	}
	else if (isCubeMapArray())
	{
		return getCubeMapFace(static_cast<uint32_t>(layer / 6), static_cast<uint32_t>(layer % 6), "Cube Map Face View");
	}
	else
	{
		spdlog::error("WebGPUTexture::getTextureView: Texture is not an array or cube map, cannot get layer view.");
		return nullptr;
	}
}

wgpu::TextureView WebGPUTexture::get2DArrayLayerView(uint32_t layerIndex, const char *label) const
{
	if (!is2DArrayLayerView())
	{
		spdlog::error("WebGPUTexture::getArrayLayerView: Texture is not a 2D array, cannot get layer view.");
		return nullptr;
	}

	// Return cached view if available
	auto it = m_layerViews.find(layerIndex);
	if (it != m_layerViews.end())
		return it->second;

	if (!m_texture)
	{
		spdlog::error("WebGPUTexture::getArrayLayerView: Cannot create layer view from null texture.");
		return nullptr;
	}

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label = label ? label : "2D Array Layer View";
	viewDesc.format = m_textureDesc.format;
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = layerIndex;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = (m_textureDesc.format == wgpu::TextureFormat::Depth24Plus || m_textureDesc.format == wgpu::TextureFormat::Depth32Float || m_textureDesc.format == wgpu::TextureFormat::Depth24PlusStencil8 || m_textureDesc.format == wgpu::TextureFormat::Depth32FloatStencil8)
						  ? wgpu::TextureAspect::DepthOnly
						  : wgpu::TextureAspect::All;

	auto view = wgpuTextureCreateView(m_texture, &viewDesc);
	m_layerViews.emplace(layerIndex, view);
	return view;
}

wgpu::TextureView WebGPUTexture::getCubeMapView(uint32_t cubeIndex, const char *label) const
{
	if (!isCubeMapArray())
	{
		spdlog::error("WebGPUTexture::getCubeMapView: Texture is not a Cube Array, cannot create cube view.");
		return nullptr;
	}

	// Return cached view if available
	auto it = m_cubeMapViews.find(cubeIndex);
	if (it != m_cubeMapViews.end())
		return it->second;

	if (!m_texture)
	{
		spdlog::error("WebGPUTexture::getCubeMapView: Cannot create cube view from null texture.");
		return nullptr;
	}
	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label = label ? label : "Texture Cube View";
	viewDesc.format = m_textureDesc.format;
	viewDesc.dimension = wgpu::TextureViewDimension::Cube;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = cubeIndex * 6; // Each cube face consists of 6 layers
	viewDesc.arrayLayerCount = 6;			 // View for one cube face
	viewDesc.aspect = m_textureDesc.format == wgpu::TextureFormat::Depth24Plus
							  || m_textureDesc.format == wgpu::TextureFormat::Depth32Float
							  || m_textureDesc.format == wgpu::TextureFormat::Depth24PlusStencil8
							  || m_textureDesc.format == wgpu::TextureFormat::Depth32FloatStencil8
						  ? wgpu::TextureAspect::DepthOnly
						  : wgpu::TextureAspect::All;

	auto view = wgpuTextureCreateView(m_texture, &viewDesc); // Use C API to create the view because of const correctness
	m_cubeMapViews.emplace(cubeIndex, view);
	return view;
}

wgpu::TextureView WebGPUTexture::getCubeMapFace(uint32_t cubeIndex, uint32_t faceIndex, const char *label) const
{
	if (!isCubeMapArray())
	{
		spdlog::error("WebGPUTexture::getCubeMapView: Texture is not a Cube Array, cannot create cube view.");
		return nullptr;
	}
	if (cubeIndex * 6 + faceIndex >= m_textureDesc.size.depthOrArrayLayers)
	{
		spdlog::error("WebGPUTexture::getCubeMapView: Invalid cubeIndex or faceIndex.");
		return nullptr;
	}
	if (faceIndex >= 6)
	{
		spdlog::error("WebGPUTexture::getCubeMapView: faceIndex must be in the range 0-5.");
		return nullptr;
	}
	uint32_t arrayLayer = cubeIndex * 6 + faceIndex;

	// Return cached view if available
	auto it = m_layerViews.find(arrayLayer);
	if (it != m_layerViews.end())
		return it->second;

	if (!m_texture)
	{
		spdlog::error("WebGPUTexture::getCubeMapView: Cannot create cube view from null texture.");
		return nullptr;
	}
	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label = label ? label : "Texture Cube Face View";
	viewDesc.format = m_textureDesc.format;
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = arrayLayer; // Each cube face consists of 6 layers
	viewDesc.arrayLayerCount = 1;		  // View for one cube face
	viewDesc.aspect = m_textureDesc.format == wgpu::TextureFormat::Depth24Plus
							  || m_textureDesc.format == wgpu::TextureFormat::Depth32Float
							  || m_textureDesc.format == wgpu::TextureFormat::Depth24PlusStencil8
							  || m_textureDesc.format == wgpu::TextureFormat::Depth32FloatStencil8
						  ? wgpu::TextureAspect::DepthOnly
						  : wgpu::TextureAspect::All;

	auto view = wgpuTextureCreateView(m_texture, &viewDesc); // Use C API to create the view because of const correctness
	m_layerViews.emplace(arrayLayer, view);
	return view;
}

bool WebGPUTexture::beginReadback(WebGPUContext &context)
{
	if (m_readbackPending)
		return false;

	m_readbackWidth = getWidth();
	m_readbackHeight = getHeight();
	m_readbackBPP = engine::resources::ImageFormat::getBytesPerPixel(
		mapGPUFormatToImageFormat(getFormat())
	);

	const uint32_t unaligned = m_readbackWidth * m_readbackBPP;
	m_readbackBytesPerRow = (unaligned + 255u) & ~255u;
	const uint64_t bufferSize = static_cast<uint64_t>(m_readbackBytesPerRow) * m_readbackHeight;

	wgpu::BufferDescriptor bufferDesc{};
	bufferDesc.size = bufferSize;
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
	m_readbackStagingBuffer = context.getDevice().createBuffer(bufferDesc);

	wgpu::CommandEncoder encoder = context.getDevice().createCommandEncoder();

	wgpu::ImageCopyTexture src{};
	src.texture = m_texture;
	src.mipLevel = 0;
	src.origin = {0, 0, 0};

	wgpu::ImageCopyBuffer dst{};
	dst.buffer = m_readbackStagingBuffer;
	dst.layout.bytesPerRow = m_readbackBytesPerRow;
	dst.layout.rowsPerImage = m_readbackHeight;
	dst.layout.offset = 0;
	encoder.copyTextureToBuffer(src, dst, {m_readbackWidth, m_readbackHeight, 1});
	wgpu::CommandBuffer commands = encoder.finish();
	context.getQueue().submit(1, &commands);

	m_readbackMapped = false;
	m_readbackSuccess = false;
	m_readbackPending = true;

	m_readbackCallback = m_readbackStagingBuffer.mapAsync(
		wgpu::MapMode::Read,
		0,
		bufferSize,
		[this](WGPUBufferMapAsyncStatus status)
		{
			m_readbackSuccess = (status == WGPUBufferMapAsyncStatus_Success);
			m_readbackMapped = true;
		}
	);

	return true;
}
bool WebGPUTexture::pollReadback(WebGPUContext &context, std::shared_ptr<Texture> outTexture)
{
	if (!m_readbackPending)
		return true;
	if (!m_readbackMapped)
		return false;

	m_readbackPending = false;
	m_readbackCallback = nullptr;

	if (!m_readbackSuccess)
	{
		spdlog::error("Texture readback failed.");
		m_readbackStagingBuffer.release();
		return true;
	}

	const uint64_t bufferSize = static_cast<uint64_t>(m_readbackBytesPerRow) * m_readbackHeight;
	const uint8_t *src = static_cast<const uint8_t *>(
		m_readbackStagingBuffer.getMappedRange(0, bufferSize)
	);

	// Output is always RGBA8Unorm (LDR) for PNG compatibility
	const uint32_t outBPP = 4; // RGBA8
	const uint32_t outBPR = m_readbackWidth * outBPP;
	std::vector<uint8_t> pixelData(m_readbackHeight * outBPR);

	const bool isHDR = (getFormat() == wgpu::TextureFormat::RGBA16Float || getFormat() == wgpu::TextureFormat::RGBA32Float);

	for (uint32_t row = 0; row < m_readbackHeight; ++row)
	{
		const uint8_t *rowSrc = src + row * m_readbackBytesPerRow;
		uint8_t *rowDst = pixelData.data() + row * outBPR;

		// Accurate linear -> sRGB conversion (IEC 61966-2-1 standard)
		auto linearToSRGB = [](float linear) -> float
		{
			linear = std::max(linear, 0.0f);
			if (linear <= 0.0031308f)
				return linear * 12.92f;
			return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
		};

		if (isHDR && getFormat() == wgpu::TextureFormat::RGBA16Float)
		{
			const uint16_t *f16Src = reinterpret_cast<const uint16_t *>(rowSrc);
			for (uint32_t x = 0; x < m_readbackWidth; ++x)
			{
				const uint32_t i = x * 4;
				float r = glm::detail::toFloat32(f16Src[i + 0]);
				float g = glm::detail::toFloat32(f16Src[i + 1]);
				float b = glm::detail::toFloat32(f16Src[i + 2]);
				float a = glm::detail::toFloat32(f16Src[i + 3]);

				rowDst[i + 0] = static_cast<uint8_t>(std::clamp(linearToSRGB(r), 0.0f, 1.0f) * 255.0f + 0.5f);
				rowDst[i + 1] = static_cast<uint8_t>(std::clamp(linearToSRGB(g), 0.0f, 1.0f) * 255.0f + 0.5f);
				rowDst[i + 2] = static_cast<uint8_t>(std::clamp(linearToSRGB(b), 0.0f, 1.0f) * 255.0f + 0.5f);
				rowDst[i + 3] = static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		}
		else if (isHDR && getFormat() == wgpu::TextureFormat::RGBA32Float)
		{
			const float *f32Src = reinterpret_cast<const float *>(rowSrc);
			for (uint32_t x = 0; x < m_readbackWidth; ++x)
			{
				const uint32_t i = x * 4;
				rowDst[i + 0] = static_cast<uint8_t>(std::clamp(linearToSRGB(f32Src[i + 0]), 0.0f, 1.0f) * 255.0f + 0.5f);
				rowDst[i + 1] = static_cast<uint8_t>(std::clamp(linearToSRGB(f32Src[i + 1]), 0.0f, 1.0f) * 255.0f + 0.5f);
				rowDst[i + 2] = static_cast<uint8_t>(std::clamp(linearToSRGB(f32Src[i + 2]), 0.0f, 1.0f) * 255.0f + 0.5f);
				rowDst[i + 3] = static_cast<uint8_t>(std::clamp(f32Src[i + 3], 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		}
		else
		{
			// LDR formats — direct copy, stripping alignment padding
			std::memcpy(rowDst, rowSrc, outBPR);
		}
	}

	m_readbackStagingBuffer.unmap();
	m_readbackStagingBuffer.release();

	auto image = std::make_shared<engine::resources::Image>(
		m_readbackWidth,
		m_readbackHeight,
		engine::resources::ImageFormat::Type::LDR_RGBA8, // always LDR output
		std::move(pixelData)
	);
	outTexture->replaceImageData(image);

	return true;
}

} // namespace engine::rendering::webgpu