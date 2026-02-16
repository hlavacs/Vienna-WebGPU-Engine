#include "engine/rendering/DebugPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupBinder.h"
#include "engine/rendering/DebugRenderCollector.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

namespace engine::rendering
{

DebugPass::DebugPass(std::shared_ptr<webgpu::WebGPUContext> context) : RenderPass(context)
{
}

bool DebugPass::initialize()
{
	spdlog::info("Initializing DebugPass");

	// Get debug line shader from registry (assume a default exists, e.g., DEBUG_PRIMITIVE)
	m_shaderInfo = m_context->shaderRegistry().getShader(shader::defaults::DEBUG);
	if (!m_shaderInfo || !m_shaderInfo->isValid())
	{
		spdlog::error("Debug primitive shader not found in registry");
		return false;
	}

	m_debugBindGroup = m_context->bindGroupFactory().createBindGroup(
		m_shaderInfo->getBindGroupLayout(bindgroup::defaults::DEBUG)
	);

	if (!m_debugBindGroup || !m_debugBindGroup->isValid())
	{
		spdlog::error("Failed to create debug primitive bind group");
		return false;
	}

	// Create sampler if needed (for textured debug primitives)
	m_sampler = m_context->samplerFactory().getClampLinearSampler();

	spdlog::info("DebugPass initialized successfully");
	return true;
}

void DebugPass::setDebugCollector(const DebugRenderCollector *collector)
{
	m_debugCollector = collector;
}

void DebugPass::setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext> &renderPassContext)
{
	if (renderPassContext->getDepthTexture())
	{
		spdlog::warn("DebugPass::setRenderPassContext() - render pass context contains a depth texture, which is not supported.");
		return;
	}
	m_renderPassContext = renderPassContext;
}

void DebugPass::render(FrameCache &frameCache)
{
	if (!m_debugCollector || m_debugCollector->isEmpty() || !m_renderPassContext)
	{
		return;
	}
	auto pipeline = m_pipeline.lock();
	if (!pipeline)
	{
		// Create pipeline using the pipeline manager
		m_pipeline = m_context->pipelineManager().getOrCreatePipeline(
			m_shaderInfo,
			m_renderPassContext->getColorTexture(0)->getFormat(),
			wgpu::TextureFormat::Undefined, // No depth for overlay
			Topology::Lines,
			wgpu::CullMode::None,
			1
		);
		pipeline = m_pipeline.lock();
	}

	spdlog::debug("DebugPass::render() - debug primitives count: {}", m_debugCollector->getPrimitives().size());

	auto primitives = m_debugCollector->getPrimitives();
	uint32_t primitiveCount = static_cast<uint32_t>(m_debugCollector->getPrimitiveCount());
	m_debugBindGroup->updateBuffer(
		0, // binding 0
		primitives.data(),
		primitiveCount * sizeof(DebugPrimitive),
		0,
		m_context->getQueue()
	);

	// Create command encoder
	auto encoder = m_context->createCommandEncoder("DebugPass Encoder");
	wgpu::RenderPassEncoder renderPass = m_renderPassContext->begin(encoder);
	{
		renderPass.setPipeline(pipeline->getPipeline());

		// Use BindGroupBinder to bind frame and debug bind groups
		BindGroupBinder binder(&frameCache);
		binder.bind(
			renderPass,
			pipeline,
			m_cameraId,
			{{BindGroupType::Debug, m_debugBindGroup}}
		);

		constexpr uint32_t maxVertexCount = 32;
		renderPass.draw(maxVertexCount, primitiveCount, 0, 0);
	}
	m_renderPassContext->end(renderPass);
	m_context->submitCommandEncoder(encoder, "DebugPass Commands");
}

void DebugPass::cleanup()
{
	// Cleanup resources if needed
}

} // namespace engine::rendering
