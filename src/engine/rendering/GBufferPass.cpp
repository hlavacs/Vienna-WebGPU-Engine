#include "engine/rendering/GBufferPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupBinder.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameProfiler.h"
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
	m_shader = getValidatedShader(shader::defaults::GBUFFER);
	if (!m_shader)
		return false;
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
	// PipelineManager owns and invalidates the pipeline cache on hot reload.
	// Drop our local per-cull-mode Handle cache so we don't hold dangling
	// slot pointers past PipelineManager shutdown.
	m_pipelinePerCullMode = {};
}

engine::rendering::cache::Handle<webgpu::WebGPUPipeline> GBufferPass::getPipelineFor(wgpu::CullMode cullMode)
{
	// Index into the per-cull-mode local cache. The G-buffer pass only
	// uses Back / None; anything else collapses to Back. The 2-slot array
	// avoids the PipelineManager SlotCache hashmap + mutex on every visible
	// item — a per-draw cost otherwise.
	const size_t idx = (cullMode == wgpu::CullMode::None) ? 1 : 0;
	auto        &cached = m_pipelinePerCullMode[idx];

	// `isAttached()` (not `valid()`) is the right test here: after a hot
	// reload the slot persists but its resource is evicted. `valid()` would
	// force a re-fetch every frame until lock() rebuilt the resource.
	// `isAttached()` only fires the fetch once per cull mode for the
	// lifetime of the pass — exactly the behavior we want.
	if (!cached.isAttached())
	{
		cached = m_context->pipelineManager().getOrCreatePipeline(
			m_shader,
			// Fallback color format; the shader's declared color-target list wins
			// inside the pipeline factory for the actual MRT setup.
			webgpu::GBuffer::FORMAT_POSITION,
			webgpu::GBuffer::FORMAT_DEPTH,
			engine::rendering::Topology::Type::Triangles,
			cullMode,
			false, // G-buffer outputs are opaque - blending is for the forward pass
			1
		);

		if (auto snap = cached.lock(); !snap || !snap->isValid())
		{
			spdlog::error("GBufferPass: failed to create pipeline for cullMode={}",
				cullMode == wgpu::CullMode::Back ? "Back" : "None");
			cached = {};
			return {};
		}
	}
	return cached;
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
	if (auto *prof = m_context->frameProfiler())
		prof->beginGpuScope("Pass.GBuffer", encoder);
	wgpu::RenderPassEncoder renderPass = passContext->begin(encoder);

	BindGroupBinder binder(&frameCache);
	binder.setContext(m_context.get());

	const auto &gpuItems = frameCache.gpuRenderItems;
	// Track the active pipeline by Handle identity (compares slot ptr, not
	// resource ptr) so that even if a hot-reload swap happens mid-pass the
	// snapshot we already pinned for the current draw stays valid.
	engine::rendering::cache::Handle<webgpu::WebGPUPipeline> currentHandle;
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
		const auto matFeatures = cpuMaterialOpt.value()->getFeatureMask();
		// Transparent items belong to ForwardTransparencyPass - here they would
		// either lose their alpha or write depth that occludes the forward pass.
		if (engine::rendering::MaterialFeature::hasFlag(
				matFeatures,
				engine::rendering::MaterialFeature::Flag::Transparent))
		{
			++skipped;
			continue;
		}
		const wgpu::CullMode cullMode = engine::rendering::MaterialFeature::hasFlag(
				matFeatures,
				engine::rendering::MaterialFeature::Flag::DoubleSided)
			? wgpu::CullMode::None
			: wgpu::CullMode::Back;

		auto pipelineHandle = getPipelineFor(cullMode);
		if (!pipelineHandle.valid())
		{
			++skipped;
			continue;
		}

		if (pipelineHandle != currentHandle)
		{
			currentHandle = pipelineHandle;
			currentPipeline = currentHandle.lock();
			if (!currentPipeline)
			{
				++skipped;
				continue;
			}
			renderPass.setPipeline(currentPipeline->getPipeline());
			// New pipeline may use a different vertex layout - drop cached mesh binding.
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
	if (auto *prof = m_context->frameProfiler())
		prof->endGpuScope("Pass.GBuffer", encoder);
	m_context->submitCommandEncoder(encoder, "GBufferPass.Commands");

	spdlog::debug("GBufferPass: rendered {} items ({} skipped)", rendered, skipped);
}

} // namespace engine::rendering
