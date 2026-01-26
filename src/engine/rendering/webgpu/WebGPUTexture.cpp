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

std::future<bool> WebGPUTexture::readbackToCPUAsync(WebGPUContext &context, std::shared_ptr<Texture> outTexture)
{
	// Capture necessary data by value for async lambda
	auto texture = m_texture;
	auto width = getWidth();
	auto height = getHeight();
	auto bytesPerPixel = engine::resources::ImageFormat::getChannelCount(
		mapGPUFormatToImageFormat(getFormat())
	);
	auto bufferSize = width * height * bytesPerPixel;

	return std::async(std::launch::async, [&, texture, width, height, bytesPerPixel, bufferSize, outTexture]() -> bool
					  {
        if (!outTexture)
            return false;

        try
        {
            // 1. Create staging buffer
            wgpu::BufferDescriptor bufferDesc{};
            bufferDesc.size = bufferSize;
            bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            wgpu::Buffer stagingBuffer = context.getDevice().createBuffer(bufferDesc);

            // 2. Copy texture to buffer
            wgpu::CommandEncoder encoder = context.getDevice().createCommandEncoder();

            wgpu::ImageCopyTexture srcTexture{};
            srcTexture.texture = texture;
            srcTexture.mipLevel = 0;
            srcTexture.origin = {0, 0, 0};

            wgpu::ImageCopyBuffer dstBuffer{};
            dstBuffer.buffer = stagingBuffer;
            dstBuffer.layout.bytesPerRow = width * bytesPerPixel;
            dstBuffer.layout.rowsPerImage = height;

            wgpu::Extent3D copySize{width, height, 1};

            encoder.copyTextureToBuffer(srcTexture, dstBuffer, copySize);

            wgpu::CommandBuffer commands = encoder.finish();
            context.getQueue().submit(1, &commands);

            // 3. Map buffer asynchronously
            std::promise<bool> resultPromise;
            auto resultFuture = resultPromise.get_future();

            stagingBuffer.mapAsync(wgpu::MapMode::Read, 0, bufferSize, [stagingBuffer, outTexture, width, height, bytesPerPixel, &resultPromise](WGPUBufferMapAsyncStatus status) mutable {
                if (status != WGPUBufferMapAsyncStatus_Success)
                {
                    spdlog::error("Failed to map buffer for reading.");
                    resultPromise.set_value(false);
                    return;
                }

                auto mappedRange = stagingBuffer.getMappedRange(0, width * height * bytesPerPixel);
                std::vector<uint8_t> pixelData(width * height * bytesPerPixel);
                std::memcpy(pixelData.data(), mappedRange, width * height * bytesPerPixel);

                // Create Image and replace texture data
                engine::resources::Image::Ptr image = std::make_shared<engine::resources::Image>(
                    width,
                    height,
                    outTexture->getImage()->getFormat(),
                    std::move(pixelData)
                );
                outTexture->replaceImageData(image);

                stagingBuffer.unmap();
                stagingBuffer.release();

                resultPromise.set_value(true);
            });

            // Wait for async map callback to complete
            return resultFuture.get();
        }
        catch (const std::exception &e)
        {
            spdlog::error("Exception during async texture readback: {}", e.what());
            return false;
        } });
}

} // namespace engine::rendering::webgpu