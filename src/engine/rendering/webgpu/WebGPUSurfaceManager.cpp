#include "engine/rendering/webgpu/WebGPUSurfaceManager.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

#include <spdlog/spdlog.h>

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
	// Configure the EXISTING surface; do NOT terminate + recreate it. The adapter
	// was requested with this surface as compatibleSurface, so a recreated one is a
	// different object Dawn rejects (getCurrentTexture status=Error -> black screen).
	// surface.configure() already handles resize by rebuilding the swap chain.
	auto surface = m_context.getSurface();
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

	// v24 reports a status alongside the texture. Dawn hands back no usable texture
	// with an Outdated/Lost status when the swap chain needs rebuilding (after the
	// SDL surface handshake or a resize); reconfigure once and retry. wgpu-native
	// returned a texture without this, so the missing check only bit Dawn (black screen).
	auto usable = [](const wgpu::SurfaceTexture &st) {
		const auto s = static_cast<uint32_t>(st.status);
		return st.texture != nullptr &&
		       (s == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
		        s == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
	};

	if (!usable(surfaceTexture))
	{
		applyConfig();
		surface.getCurrentTexture(&surfaceTexture);
		if (!usable(surfaceTexture))
		{
			spdlog::warn("[Surface] getCurrentTexture unusable (status={})",
			             static_cast<uint32_t>(surfaceTexture.status));
			return nullptr;
		}
	}

	// Render through the reinterpreted view format when one is configured (sRGB view
	// of a non-sRGB swap chain, see WebGPUContext::initDevice); else the base format.
	const wgpu::TextureFormat renderFormat = m_config.viewFormats.empty() ? m_config.format : m_config.viewFormats[0];

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.dimension = wgpu::TextureViewDimension::_2D;
	viewDesc.format = renderFormat;
	viewDesc.mipLevelCount = 1;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = wgpu::TextureAspect::All;

	wgpu::TextureView nextTexture = wgpu::Texture(surfaceTexture.texture).createView(viewDesc);

	// Build descriptors based on current config and acquired texture
	wgpu::TextureDescriptor texDesc{};
	texDesc.size.width = m_config.width;
	texDesc.size.height = m_config.height;
	texDesc.size.depthOrArrayLayers = 1;
	texDesc.dimension = wgpu::TextureDimension::_2D;
	texDesc.format = renderFormat;
	texDesc.mipLevelCount = 1;
	texDesc.sampleCount = 1;
	texDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;

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
