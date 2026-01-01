#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include <memory>
#include <spdlog/spdlog.h>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

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