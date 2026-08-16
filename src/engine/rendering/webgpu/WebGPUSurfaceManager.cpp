#include "engine/rendering/webgpu/WebGPUSurfaceManager.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

#include <iostream>

namespace engine::rendering::webgpu
{

WebGPUSurfaceManager::WebGPUSurfaceManager(WebGPUContext &context) : m_context(context)
{
}

bool WebGPUSurfaceManager::updateIfNeeded(uint32_t width, uint32_t height)
{
	m_config.width = width;
	m_config.height = height;

	if (m_config != m_lastAppliedConfig)
	{
		applyConfig();
		return true;
	}

	return false;
}

void WebGPUSurfaceManager::reconfigure(const std::optional<Config> &config)
{
	if (config.has_value())
	{
		m_config = config.value();
	}

	applyConfig();
	m_lastAppliedConfig = m_config;
}

void WebGPUSurfaceManager::applyConfig()
{
	m_context.terminateSurface();
	auto surface = m_context.getSurface();

	// Modern surface API for all backends (Dawn/current webgpu.h removed SwapChain).
	wgpu::SurfaceConfiguration cfg = m_config.asSurfaceConfiguration(m_context.getDevice());
	surface.configure(cfg);

	m_lastAppliedConfig = m_config;
}

std::shared_ptr<WebGPUTexture> WebGPUSurfaceManager::acquireNextTexture()
{
	// ToDo: Handle lost surface/swap-chain
	auto surface = m_context.getSurface();
	// ensure surface is up-to-date
	if (m_config != m_lastAppliedConfig)
		applyConfig();

	wgpu::SurfaceTexture surfaceTexture{};
	surface.getCurrentTexture(&surfaceTexture);

	if (!surfaceTexture.texture)
		return nullptr;

	wgpu::TextureView nextTexture = wgpu::Texture(surfaceTexture.texture).createView();

	// Build descriptors based on current config and acquired texture
	wgpu::TextureDescriptor texDesc{};
	texDesc.size.width = m_config.width;
	texDesc.size.height = m_config.height;
	texDesc.size.depthOrArrayLayers = 1;
	texDesc.dimension = wgpu::TextureDimension::_2D;
	texDesc.format = m_config.format;
	texDesc.mipLevelCount = 1;
	texDesc.sampleCount = 1;
	texDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
	viewDesc.format = m_config.format;
	viewDesc.mipLevelCount = 1;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = wgpu::TextureAspect::All;

	// manually construct shared WebGPUTexture
	return std::make_shared<WebGPUTexture>(
		nullptr,
		nextTexture,
		texDesc,
		viewDesc,
		Texture::Type::Surface
	);
}

} // namespace engine::rendering::webgpu
