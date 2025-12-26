#include "engine/rendering/webgpu/WebGPUTexture.h" // your CPU-side texture
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

	if (m_isSurfaceTexture) {
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
	if (newViewDesc.format == wgpu::TextureFormat::Undefined)
	{
		newViewDesc.dimension = wgpu::TextureViewDimension::_2D;
		newViewDesc.aspect = m_isDepthTexture ? wgpu::TextureAspect::DepthOnly : wgpu::TextureAspect::All;
		newViewDesc.baseMipLevel = 0;
		newViewDesc.mipLevelCount = 1;
		newViewDesc.baseArrayLayer = 0;
		newViewDesc.arrayLayerCount = 1;
	}
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

bool WebGPUTexture::readbackToCPU(WebGPUContext &context, std::shared_ptr<Texture> outTexture)
{
	if (!outTexture)
		return false;

	try
	{

		// Assume RGBA8 format for simplicity
		size_t bytesPerPixel = 4;
		size_t bufferSize = getWidth() * getHeight() * bytesPerPixel;

		// 1. Create staging buffer for GPU->CPU copy
		wgpu::BufferDescriptor bufferDesc{};
		bufferDesc.size = bufferSize;
		bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
		wgpu::Buffer stagingBuffer = context.getDevice().createBuffer(bufferDesc);

		wgpu::CommandEncoder encoder = context.getDevice().createCommandEncoder();

		wgpu::ImageCopyTexture srcTexture{};
		srcTexture.texture = m_texture;
		srcTexture.mipLevel = 0;
		srcTexture.origin = {0, 0, 0};

		wgpu::ImageCopyBuffer dstBuffer{};
		dstBuffer.buffer = stagingBuffer;
		dstBuffer.layout.bytesPerRow = getWidth() * bytesPerPixel;
		dstBuffer.layout.rowsPerImage = getHeight();

		wgpu::Extent3D copySize{};
		copySize.width = getWidth();
		copySize.height = getHeight();
		copySize.depthOrArrayLayers = 1;

		encoder.copyTextureToBuffer(srcTexture, dstBuffer, copySize);

		wgpu::CommandBuffer commands = encoder.finish();
		context.getQueue().submit(1, &commands);
		static auto mappedCallback = [](WGPUBufferMapAsyncStatus status)
		{
			if (status != WGPUBufferMapAsyncStatus_Success)
			{
				spdlog::error("Failed to map buffer for reading.");
			}
		};
		stagingBuffer.mapAsync(wgpu::MapMode::Read, 0, bufferSize, mappedCallback);
		auto mappedRange = stagingBuffer.getMappedRange(0, bufferSize);

		std::vector<uint8_t> pixelData(bufferSize);
		std::memcpy(pixelData.data(), mappedRange, bufferSize);

		outTexture->replaceData(
			std::move(pixelData),
			getWidth(),
			getHeight(),
			outTexture->getChannels()
		);

		stagingBuffer.unmap();
		stagingBuffer.release();
		return true;
	}
	catch (const std::exception &e)
	{
		spdlog::error("Exception during texture readback: {}", e.what());
		return false;
	}
}
} // namespace engine::rendering::webgpu