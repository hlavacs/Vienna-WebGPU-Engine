#include "engine/rendering/GBufferPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupBinder.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h" // for engine::rendering::Topology
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/GBuffer.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{

GBufferPass::GBufferPass(
	std::shared_ptr<webgpu::WebGPUContext> context,
	uint32_t initialWidth,
	uint32_t initialHeight
) :
	RenderPass(std::move(context)),
	m_gBuffer(std::make_unique<webgpu::GBuffer>(*m_context, initialWidth, initialHeight))
{
}

bool GBufferPass::initialize()
{
	m_shader = m_context->shaderRegistry().getShader(shader::defaults::GBUFFER);
	if (!m_shader || !m_shader->isValid())
	{
		spdlog::error("GBufferPass: '{}' shader is missing or invalid", shader::defaults::GBUFFER);
		return false;
	}
	spdlog::info("GBufferPass initialized ({}x{})", m_gBuffer->getWidth(), m_gBuffer->getHeight());
	return true;
}

bool GBufferPass::resize(uint32_t width, uint32_t height)
{
	if (width == m_gBuffer->getWidth() && height == m_gBuffer->getHeight())
		return false;

	m_gBuffer->resize(width, height);
	// Pipeline state itself does not depend on size, but the cached render-pass
	// context inside GBuffer was invalidated; the pipeline is still reusable.
	return true;
}

void GBufferPass::cleanup()
{
	// Drop the cached pipeline references - the pipeline manager owns the
	// actual GPU objects and reuses them via PipelineKey, so this is safe.
	m_pipelineCache.clear();
}

std::shared_ptr<webgpu::WebGPUPipeline> GBufferPass::getPipelineFor(wgpu::CullMode cullMode)
{
	// Cache one pipeline per cull mode. There are at most two variants
	// (Back-face culling for sealed opaque meshes, None for double-sided
	// materials), so a tiny vector is the right container - no hashing
	// overhead, no surprise allocations, and the pipeline manager still
	// keys its own cache by full PipelineKey anyway.
	for (auto &entry : m_pipelineCache)
	{
		if (entry.cullMode == cullMode)
			return entry.pipeline;
	}

	auto pipeline = m_context->pipelineManager().getOrCreatePipeline(
		m_shader,
		// Single fallback color format; the shader's declared color-target
		// list wins inside the pipeline factory for the actual MRT setup.
		webgpu::GBuffer::FORMAT_POSITION,
		webgpu::GBuffer::FORMAT_DEPTH,
		engine::rendering::Topology::Type::Triangles,
		cullMode,
		false, // G-buffer outputs are opaque - blending is for the forward pass
		1
	);

	if (!pipeline || !pipeline->isValid())
	{
		spdlog::error("GBufferPass: failed to create pipeline for cullMode={}",
			cullMode == wgpu::CullMode::Back ? "Back" : "None");
		return nullptr;
	}

	m_pipelineCache.push_back({cullMode, pipeline});
	return pipeline;
}

void GBufferPass::render(FrameCache &frameCache)
{
	if (!m_shader)
	{
		spdlog::error("GBufferPass::render called before initialize()");
		return;
	}

	auto passContext = m_gBuffer->getRenderPassContext();
	if (!passContext)
	{
		spdlog::error("GBufferPass: failed to acquire render-pass context");
		return;
	}

	auto encoder = m_context->createCommandEncoder("GBufferPass.Encoder");
	wgpu::RenderPassEncoder renderPass = passContext->begin(encoder);

	BindGroupBinder binder(&frameCache);

	const auto &gpuItems = frameCache.gpuRenderItems;
	std::shared_ptr<webgpu::WebGPUPipeline> currentPipeline;
	webgpu::WebGPUMesh *currentMesh = nullptr;
	size_t rendered = 0;
	size_t skipped = 0;

	for (size_t index : m_visibleIndices)
	{
		if (index >= gpuItems.size() || !gpuItems[index].has_value())
		{
			++skipped;
			continue;
		}

		const auto &item = gpuItems[index].value();
		if (!item.gpuMesh || !item.gpuMaterial || !item.objectBindGroup)
		{
			++skipped;
			continue;
		}

		auto cpuMaterialOpt = item.gpuMaterial->getCPUHandle().get();
		if (!cpuMaterialOpt.has_value())
		{
			++skipped;
			continue;
		}
		const auto &cpuMaterial = cpuMaterialOpt.value();
		const auto featureMask = cpuMaterial->getFeatureMask();

		// NOTE: we intentionally do NOT skip Transparent-flagged materials here.
		// GLTF assets routinely mark effectively-opaque geometry as
		// alphaMode=BLEND (the SeaKeep "Fortress" walls are the textbook
		// case). The alpha-test discard in g_buffer.wgsl handles cutout
		// coverage; truly blended primitives (glass, smoke) would need a
		// dedicated forward transparency pass, but the demo content does
		// not have any so this is the right default.
		(void)featureMask;

		// Per-material cull mode: DoubleSided materials (very common in
		// GLTF assets) must skip back-face culling, otherwise the visible
		// faces end up culled and only inside-facing geometry contributes
		// to the G-buffer.
		const wgpu::CullMode cullMode = engine::rendering::MaterialFeature::hasFlag(
				cpuMaterial->getFeatureMask(),
				engine::rendering::MaterialFeature::Flag::DoubleSided)
			? wgpu::CullMode::None
			: wgpu::CullMode::Back;

		auto pipeline = getPipelineFor(cullMode);
		if (!pipeline)
		{
			++skipped;
			continue;
		}

		if (pipeline != currentPipeline)
		{
			currentPipeline = pipeline;
			renderPass.setPipeline(currentPipeline->getPipeline());
			// New pipeline invalidates the cached vertex-buffer binding too
			// (vertex layout may differ between pipelines in principle).
			currentMesh = nullptr;
		}

		const uint64_t materialId = reinterpret_cast<uint64_t>(item.gpuMaterial.get());
		binder.bind(
			renderPass,
			currentPipeline,
			m_cameraId,
			{
				{BindGroupType::Object, item.objectBindGroup},
				{BindGroupType::Material, item.gpuMaterial->getBindGroup()},
			},
			item.objectID,
			materialId
		);

		if (item.gpuMesh != currentMesh)
		{
			currentMesh = item.gpuMesh;
			currentMesh->bindBuffers(renderPass, currentPipeline->getVertexLayout());
		}

		if (currentMesh->isIndexed())
			renderPass.drawIndexed(item.submesh.indexCount, 1, item.submesh.indexOffset, 0, 0);
		else
			renderPass.draw(item.submesh.indexCount, 1, item.submesh.indexOffset, 0);

		++rendered;
	}

	passContext->end(renderPass);
	m_context->submitCommandEncoder(encoder, "GBufferPass.Commands");

	spdlog::debug("GBufferPass: rendered {} items ({} skipped)", rendered, skipped);
}

} // namespace engine::rendering
