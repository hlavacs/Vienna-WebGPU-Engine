#include "engine/rendering/webgpu/GBuffer.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu
{

GBuffer::GBuffer(WebGPUContext &context, uint32_t width, uint32_t height) :
	m_context(context),
	m_width(width),
	m_height(height)
{
	createTextures();
}

void GBuffer::resize(uint32_t width, uint32_t height)
{
	if (m_width == width && m_height == height)
	{
		return; // No resize needed
	}

	m_width = width;
	m_height = height;
	createTextures();
}

void GBuffer::createTextures()
{
	auto device = m_context.getDevice();

	// Position: R16G16B16A16Float (world space + depth for clustering)
	{
		wgpu::TextureDescriptor txDesc{};
		txDesc.label = "GBuffer_Position";
		txDesc.size = {m_width, m_height, 1};
		txDesc.mipLevelCount = 1;
		txDesc.sampleCount = 1;
	txDesc.dimension = wgpu::TextureDimension::_2D;
	txDesc.format = wgpu::TextureFormat::RGBA16Float;
	txDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label = "GBuffer_Position_View";
	viewDesc.format = txDesc.format;
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
		viewDesc.mipLevelCount = 1;
		viewDesc.arrayLayerCount = 1;

		auto gpuTexture = device.createTexture(txDesc);
		auto gpuView = gpuTexture.createView(viewDesc);
		
		m_positionTexture = std::make_shared<WebGPUTexture>(
			gpuTexture,
			gpuView,
			txDesc,
			viewDesc
		);
	}

	// Normal: R16G16B16A16Float (normalized + depth for clustering)
	{
		wgpu::TextureDescriptor txDesc{};
		txDesc.label = "GBuffer_Normal";
		txDesc.size = {m_width, m_height, 1};
		txDesc.mipLevelCount = 1;
		txDesc.sampleCount = 1;
		txDesc.dimension = wgpu::TextureDimension::_2D;
		txDesc.format = wgpu::TextureFormat::RGBA16Float;
		txDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;

		wgpu::TextureViewDescriptor viewDesc{};
		viewDesc.label = "GBuffer_Normal_View";
		viewDesc.format = txDesc.format;
		viewDesc.dimension = wgpu::TextureViewDimension::_2D;
		viewDesc.mipLevelCount = 1;
		viewDesc.arrayLayerCount = 1;

		auto gpuTexture = device.createTexture(txDesc);
		auto gpuView = gpuTexture.createView(viewDesc);
		
		m_normalTexture = std::make_shared<WebGPUTexture>(
			gpuTexture,
			gpuView,
			txDesc,
			viewDesc
		);
	}

	// Albedo: R8G8B8A8Unorm (sRGB)
	{
		wgpu::TextureDescriptor txDesc{};
		txDesc.label = "GBuffer_Albedo";
		txDesc.size = {m_width, m_height, 1};
		txDesc.mipLevelCount = 1;
		txDesc.sampleCount = 1;
		txDesc.dimension = wgpu::TextureDimension::_2D;
		txDesc.format = wgpu::TextureFormat::RGBA8UnormSrgb;
		txDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;

		wgpu::TextureViewDescriptor viewDesc{};
		viewDesc.label = "GBuffer_Albedo_View";
		viewDesc.format = txDesc.format;
		viewDesc.dimension = wgpu::TextureViewDimension::_2D;
		viewDesc.mipLevelCount = 1;
		viewDesc.arrayLayerCount = 1;

		auto gpuTexture = device.createTexture(txDesc);
		auto gpuView = gpuTexture.createView(viewDesc);
		
		m_albedoTexture = std::make_shared<WebGPUTexture>(
			gpuTexture,
			gpuView,
			txDesc,
			viewDesc
		);
	}

	// Material: R8G8B8A8Unorm (R=roughness, G=metallic, B=AO, A=unused)
	{
		wgpu::TextureDescriptor txDesc{};
		txDesc.label = "GBuffer_Material";
		txDesc.size = {m_width, m_height, 1};
		txDesc.mipLevelCount = 1;
		txDesc.sampleCount = 1;
		txDesc.dimension = wgpu::TextureDimension::_2D;
		txDesc.format = wgpu::TextureFormat::RGBA8Unorm;
		txDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;

		wgpu::TextureViewDescriptor viewDesc{};
		viewDesc.label = "GBuffer_Material_View";
		viewDesc.format = txDesc.format;
		viewDesc.dimension = wgpu::TextureViewDimension::_2D;
		viewDesc.mipLevelCount = 1;
		viewDesc.arrayLayerCount = 1;

		auto gpuTexture = device.createTexture(txDesc);
		auto gpuView = gpuTexture.createView(viewDesc);
		
		m_materialTexture = std::make_shared<WebGPUTexture>(
			gpuTexture,
			gpuView,
			txDesc,
			viewDesc
		);
	}

	// Depth texture
	{
		wgpu::TextureDescriptor txDesc{};
		txDesc.label = "GBuffer_Depth";
		txDesc.size = {m_width, m_height, 1};
		txDesc.mipLevelCount = 1;
		txDesc.sampleCount = 1;
		txDesc.dimension = wgpu::TextureDimension::_2D;
		txDesc.format = wgpu::TextureFormat::Depth24Plus;
		txDesc.usage = wgpu::TextureUsage::RenderAttachment;

		wgpu::TextureViewDescriptor viewDesc{};
		viewDesc.label = "GBuffer_Depth_View";
		viewDesc.format = txDesc.format;
		viewDesc.dimension = wgpu::TextureViewDimension::_2D;
		viewDesc.mipLevelCount = 1;
		viewDesc.arrayLayerCount = 1;

		auto gpuTexture = device.createTexture(txDesc);
		auto gpuView = gpuTexture.createView(viewDesc);
		
		m_depthTexture = std::make_shared<WebGPUTexture>(
			gpuTexture,
			gpuView,
			txDesc,
			viewDesc,
			engine::rendering::Texture::Type::Depth
		);
	}

	spdlog::debug("GBuffer textures created: {}x{}", m_width, m_height);
}

wgpu::RenderPassColorAttachment GBuffer::getPositionAttachment() const
{
	wgpu::RenderPassColorAttachment attachment{};
	if (m_positionTexture)
	{
		// Note: view access via WebGPUTexture - would need to implement getView()
		// For now, this is a placeholder - actual MeshPass implementation will handle texture views
	}
	return attachment;
}

wgpu::RenderPassColorAttachment GBuffer::getNormalAttachment() const
{
	wgpu::RenderPassColorAttachment attachment{};
	return attachment;
}

wgpu::RenderPassColorAttachment GBuffer::getAlbedoAttachment() const
{
	wgpu::RenderPassColorAttachment attachment{};
	return attachment;
}

wgpu::RenderPassColorAttachment GBuffer::getMaterialAttachment() const
{
	wgpu::RenderPassColorAttachment attachment{};
	return attachment;
}

std::array<wgpu::RenderPassColorAttachment, 4> GBuffer::getAllColorAttachments() const
{
	return {
		getPositionAttachment(),
		getNormalAttachment(),
		getAlbedoAttachment(),
		getMaterialAttachment()
	};
}

wgpu::RenderPassDepthStencilAttachment GBuffer::getDepthAttachment() const
{
	wgpu::RenderPassDepthStencilAttachment attachment{};
	return attachment;
}

} // namespace engine::rendering::webgpu
