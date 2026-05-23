#include "engine/rendering/GBufferPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/webgpu/GBuffer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering
{

GBufferPass::GBufferPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	MeshPass(context)
{
}

bool GBufferPass::initialize()
{
	// Call parent initialization (handles light bind group setup - may be unused in deferred)
	if (!MeshPass::initialize())
	{
		return false;
	}

	// Create G-buffer with default size (1920x1080)
	m_gBuffer = std::make_shared<webgpu::GBuffer>(*m_context, 1920, 1080);

	if (!m_gBuffer)
	{
		spdlog::error("Failed to create G-buffer");
		return false;
	}

	spdlog::info("GBufferPass initialized successfully");
	return true;
}

void GBufferPass::resizeGBuffer(uint32_t width, uint32_t height)
{
	if (m_gBuffer)
	{
		m_gBuffer->resize(width, height);
	}
}

} // namespace engine::rendering

