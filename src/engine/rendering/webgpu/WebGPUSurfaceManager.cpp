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

	// terminate/recreate surface if backend requires
#ifndef WEBGPU_BACKEND_WGPU
	if (m_swapChain)
		m_swapChain = nullptr;
#endif

	// configure/reconfigure the surface
#ifdef WEBGPU_BACKEND_WGPU
	wgpu::SurfaceConfiguration cfg = m_config.asSurfaceConfiguration(m_context.getDevice());
	surface.configure(cfg);
#else
	wgpu::SwapChainDescriptor desc{};
	desc.width = m_config.width;
	desc.height = m_config.height;
	desc.format = m_config.format;
	desc.usage = m_config.usage;
	desc.presentMode = m_config.presentMode;

	m_swapChain = m_context.getDevice().createSwapChain(m_surface, desc);
#endif

	m_lastAppliedConfig = m_config;
}

std::shared_ptr<WebGPUTexture> WebGPUSurfaceManager::acquireNextTexture()
{
	// ToDo: Handle lost surface/swap-chain
	auto surface = m_context.getSurface();
	// ensure surface is up-to-date
	if (m_config != m_lastAppliedConfig)
		applyConfig();

#ifdef WEBGPU_BACKEND_WGPU
	wgpu::SurfaceTexture surfaceTexture{};
	surface.getCurrentTexture(&surfaceTexture);

	if (!surfaceTexture.texture)
	{
		// ToDo: std::cerr << "[WebGPUSurfaceManager] Failed to acquire surface texture.\n";
		return nullptr;
	}

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
		viewDesc
	);

#else
	wgpu::TextureView view = m_swapChain.getCurrentTextureView();
	if (!view)
	{
		std::cerr << "[WebGPUSurfaceManager] Failed to acquire swapchain texture view.\n";
		return nullptr;
	}

	wgpu::TextureDescriptor texDesc{};
	texDesc.size.width = m_config.width;
	texDesc.size.height = m_config.height;
	texDesc.size.depthOrArrayLayers = 1;
	texDesc.dimension = wgpu::TextureDimension::e2D;
	texDesc.format = m_config.format;
	texDesc.mipLevelCount = 1;
	texDesc.sampleCount = 1;
	texDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.dimension = wgpu::TextureViewDimension::e2D;
	viewDesc.format = m_config.format;
	viewDesc.mipLevelCount = 1;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = wgpu::TextureAspect::All;

	return std::make_shared<WebGPUTexture>(
		nullptr,
		nextTexture,
		texDesc,
		viewDesc
	);
#endif
}

} // namespace engine::rendering::webgpu
