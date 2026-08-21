#include "engine/rendering/DebugPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupBinder.h"
#include "engine/rendering/DebugRenderCollector.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameProfiler.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

#ifdef None
#undef None
#endif

namespace engine::rendering
{

DebugPass::DebugPass(std::shared_ptr<webgpu::WebGPUContext> context) : RenderPass(context)
{
}

bool DebugPass::initialize()
{
	spdlog::info("Initializing DebugPass");

	m_shaderInfo = getValidatedShader(shader::defaults::DEBUG);
	if (!m_shaderInfo)
		return false;

	// Preallocate the primitive buffer at capacity and hand it to the factory:
	// auto-created buffers are sized minBindingSize = one array element.
	m_debugPrimitiveBuffer = m_context->bufferFactory().createStorageBuffer(
		"DebugPrimitives", 0, MAX_DEBUG_PRIMITIVES * sizeof(DebugPrimitive));
	std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> overrides;
	overrides.emplace(webgpu::BindGroupBindingKey{0, 0}, webgpu::BindGroupResource(m_debugPrimitiveBuffer));
	m_debugBindGroup = m_context->bindGroupFactory().createBindGroup(
		m_shaderInfo->getBindGroupLayout(bindgroup::defaults::DEBUG),
		overrides
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
			false, // No blending for debug primitives
			1
		);
		pipeline = m_pipeline.lock();
	}

	spdlog::debug("DebugPass::render() - debug primitives count: {}", m_debugCollector->getPrimitives().size());

	auto primitives = m_debugCollector->getPrimitives();
	uint32_t primitiveCount = static_cast<uint32_t>(m_debugCollector->getPrimitiveCount());
	if (primitiveCount > MAX_DEBUG_PRIMITIVES)
	{
		spdlog::warn("DebugPass: {} primitives exceed capacity {}; extra ones are dropped", primitiveCount, MAX_DEBUG_PRIMITIVES);
		primitiveCount = MAX_DEBUG_PRIMITIVES;
	}
	m_debugBindGroup->updateBuffer(
		0, // binding 0
		primitives.data(),
		primitiveCount * sizeof(DebugPrimitive),
		0
	);

	// Create command encoder
	auto encoder = m_context->createCommandEncoder("DebugPass Encoder");
	if (auto *prof = m_context->frameProfiler())
		prof->beginGpuScope("Pass.Debug", encoder);
	wgpu::RenderPassEncoder renderPass = m_renderPassContext->begin(encoder);
	{
		renderPass.setPipeline(pipeline->getPipeline());

		// Use BindGroupBinder to bind frame and debug bind groups
		BindGroupBinder binder(&frameCache);
		binder.setContext(m_context.get());
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
	if (auto *prof = m_context->frameProfiler())
		prof->endGpuScope("Pass.Debug", encoder);
	m_context->submitCommandEncoder(encoder, "DebugPass Commands");
}

void DebugPass::cleanup()
{
	// Cleanup resources if needed
}

} // namespace engine::rendering
