#include "engine/rendering/webgpu/GBuffer.h"

#include <array>
#include <cassert>
#include <vector>

#include <spdlog/spdlog.h>

#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUDepthTextureFactory.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPURenderPassFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering::webgpu
{

GBuffer::GBuffer(WebGPUContext &context, uint32_t width, uint32_t height) :
	m_context(context),
	m_width(width),
	m_height(height)
{
	// A zero-sized G-buffer is invalid: WebGPU will reject the texture creation.
	assert(width > 0 && height > 0 && "GBuffer cannot be created with zero size");
	createTextures();
}

void GBuffer::resize(uint32_t width, uint32_t height)
{
	if (width == m_width && height == m_height)
		return;

	assert(width > 0 && height > 0 && "GBuffer cannot be resized to zero size");

	m_width = width;
	m_height = height;
	createTextures();

	// Cached pass context references the old texture views; drop it so
	// getRenderPassContext() rebuilds against the new attachments.
	m_cachedPassContext.reset();
}

void GBuffer::createTextures()
{
	auto &texFactory = m_context.textureFactory();

	struct ColorSlot
	{
		const char *label;
		wgpu::TextureFormat format;
	};
	const std::array<ColorSlot, COLOR_ATTACHMENT_COUNT> slots{{
		{"GBuffer.Position", FORMAT_POSITION},
		{"GBuffer.Normal", FORMAT_NORMAL},
		{"GBuffer.Albedo", FORMAT_ALBEDO},
		{"GBuffer.Material", FORMAT_MATERIAL},
		{"GBuffer.Emission", FORMAT_EMISSION},
	}};

	for (size_t i = 0; i < COLOR_ATTACHMENT_COUNT; ++i)
	{
		m_colorTextures[i] = texFactory.createColorRenderTarget(
			slots[i].label,
			m_width,
			m_height,
			slots[i].format
		);
	}

	// Depth target also gets TextureBinding so future passes (SSAO, soft
	// particles, depth-based composition) can sample it without rebuild.
	m_depthTexture = m_context.depthTextureFactory().createDepthTarget(
		"GBuffer.Depth",
		m_width,
		m_height,
		FORMAT_DEPTH,
		WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
	);

	spdlog::debug("GBuffer attachments (re)created at {}x{}", m_width, m_height);
}

std::shared_ptr<WebGPURenderPassContext> GBuffer::getRenderPassContext(const glm::vec4 &colorClear)
{
	// Reuse the cached context when the clear color is unchanged - the
	// attachments themselves do not change between frames unless resize() ran.
	if (m_cachedPassContext && m_cachedClearColor == colorClear)
		return m_cachedPassContext;

	std::vector<std::shared_ptr<WebGPUTexture>> colorTextures(
		m_colorTextures.begin(),
		m_colorTextures.end()
	);

	m_cachedPassContext = m_context.renderPassFactory().createMultiTarget(
		"GBuffer.RenderPass",
		colorTextures,
		m_depthTexture,
		colorClear
	);
	m_cachedClearColor = colorClear;
	return m_cachedPassContext;
}

} // namespace engine::rendering::webgpu
