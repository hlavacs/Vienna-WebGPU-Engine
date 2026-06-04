#include "engine/rendering/CompositePass.h"

#include <algorithm>
#include <array>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameProfiler.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

#ifdef None
#undef None
#endif

namespace engine::rendering
{

CompositePass::CompositePass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(context)
{
}

bool CompositePass::initialize()
{
	spdlog::info("Initializing CompositePass");

	// Get fullscreen quad shader from registry
	m_shaderInfo = m_context->shaderRegistry().getShader(shader::defaults ::FULLSCREEN_QUAD);
	if (!m_shaderInfo || !m_shaderInfo->isValid())
	{
		spdlog::error("Fullscreen quad shader not found in registry");
		return false;
	}

	// Create pipeline using the pipeline manager
	m_pipeline = m_context->pipelineManager().getOrCreatePipeline(
		m_shaderInfo,
		m_context->surfaceManager().currentConfig().format,
		wgpu::TextureFormat::Undefined, // No depth
		Topology::Triangles,
		wgpu::CullMode::None,
		true,
		1
	);

	if (auto pipe = m_pipeline.lock(); !pipe || !pipe->isValid())
	{
		spdlog::error("Failed to create fullscreen quad pipeline");
		return false;
	}

	m_sampler = m_context->samplerFactory().getClampLinearSampler();

	// Tonemap settings are global, so one shared post-process bind group lives
	// here instead of one per render target.
	m_postUniformBuffer = m_context->bufferFactory().createUniformBuffer(
		"PostProcessUniforms",
		0,
		sizeof(glm::vec4)
	);

	auto postLayout = m_shaderInfo->getBindGroupLayout(5);
	if (!postLayout || !m_postUniformBuffer)
	{
		spdlog::error("CompositePass: failed to create post-process bind group / buffer");
		return false;
	}

	std::vector<wgpu::BindGroupEntry> postEntries;
	postEntries.reserve(1);
	{
		wgpu::BindGroupEntry e{};
		e.binding = 0;
		e.buffer  = m_postUniformBuffer->getBuffer();
		e.offset  = 0;
		e.size    = sizeof(glm::vec4);
		postEntries.push_back(e);
	}

	wgpu::BindGroup rawPostBindGroup = m_context->bindGroupFactory().createBindGroup(
		postLayout->getLayout(),
		postEntries
	);

	m_postBindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
		rawPostBindGroup,
		postLayout,
		std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{m_postUniformBuffer}
	);

	spdlog::info("CompositePass initialized successfully");
	return true;
}

void CompositePass::setHDREnabled(bool enabled)
{
	if (m_hdrEnabled == enabled)
		return;
	m_hdrEnabled = enabled;
	m_postDirty = true;
}

void CompositePass::setExposure(float exposure)
{
	exposure = std::max(exposure, 0.0f);
	if (m_exposure == exposure)
		return;
	m_exposure = exposure;
	m_postDirty = true;
}

void CompositePass::flushPostProcessUniformsIfDirty()
{
	if (!m_postDirty || !m_postUniformBuffer)
		return;

	// Layout must match PostProcessUniforms in fullscreen_quad.wgsl.
	const std::array<float, 4> params{
		m_exposure,
		m_hdrEnabled ? 1.0f : 0.0f,
		0.0f,
		0.0f
	};
	m_context->getQueue().writeBuffer(
		m_postUniformBuffer->getBuffer(),
		0,
		params.data(),
		sizeof(params)
	);
	m_postDirty = false;
}

void CompositePass::render(FrameCache &frameCache)
{
	if (!m_renderPassContext || frameCache.renderTargets.empty())
	{
		spdlog::error("CompositePass: Invalid render pass context or empty targets");
		return;
	}

	const auto &surfaceTex = m_renderPassContext->getColorTexture(0);
	if (!surfaceTex)
	{
		spdlog::error("CompositePass: Render pass context has no color texture");
		return;
	}

	// Upload HDR settings if the user changed them since last frame. Cheap
	// equality check - writeBuffer is skipped when nothing changed.
	flushPostProcessUniformsIfDirty();

	auto encoder = m_context->createCommandEncoder("CompositePass Encoder");
	if (auto *prof = m_context->frameProfiler())
		prof->beginGpuScope("Pass.Composite", encoder);
	// Pin a pipeline snapshot for the lifetime of this pass. Hot reload swaps
	// the slot's resource; pinning ensures the in-flight GPU work keeps the
	// pipeline it started with even if a swap happens mid-frame.
	auto pipelineSnapshot = m_pipeline.lock();
	if (!pipelineSnapshot)
	{
		spdlog::error("CompositePass::render: pipeline handle is empty");
		return;
	}
	auto renderPass = m_renderPassContext->begin(encoder);
	renderPass.setPipeline(pipelineSnapshot->getPipeline());

	// Group 1 (post-process settings) is identical for every per-camera draw -
	// bind it once for the whole pass instead of per iteration.
	// Pipeline layout has empty placeholders at slots 0..3 (engine convention
	// reserves those for Frame/Scene/Material/Object); fullscreen_quad uses
	// @4 + @5 only. Bind the shared empty group to satisfy wgpu's "every
	// pipeline slot must have a bound bind group" requirement.
	auto emptyBg = m_context->pipelineManager().getOrCreateEmptyBindGroup();
	for (uint32_t slot = 0; slot < 4; ++slot)
		renderPass.setBindGroup(slot, emptyBg, 0, nullptr);
	if (m_postBindGroup)
		renderPass.setBindGroup(5, m_postBindGroup->getBindGroup(), 0, nullptr);

	const uint32_t surfaceW = surfaceTex->getWidth();
	const uint32_t surfaceH = surfaceTex->getHeight();

	for (const auto &[targetId, target] : frameCache.renderTargets)
	{
		auto renderToTexture = frameCache.finalTextures[targetId];
		if (!renderToTexture)
		{
			spdlog::warn("CompositePass: No final texture found for target {}, skipping", targetId);
			continue;
		}

		float x = target.viewport.min.x * surfaceW;
		float y = target.viewport.min.y * surfaceH;
		float w = target.viewport.width() * surfaceW;
		float h = target.viewport.height() * surfaceH;

		renderPass.setViewport(x, y, w, h, 0.f, 1.f);
		renderPass.setScissorRect(uint32_t(x), uint32_t(y), uint32_t(w), uint32_t(h));

		// --- Get or create bind group for this texture ---
		auto bindGroup = getOrCreateBindGroup(renderToTexture, target.layerIndex);
		if (!bindGroup)
		{
			spdlog::warn("CompositePass: Failed to create bind group for texture");
			continue;
		}

		// --- Bind the texture bind group (group 0) ---
		renderPass.setBindGroup(4, bindGroup->getBindGroup(), 0, nullptr);

		// --- Draw fullscreen triangle constrained by viewport ---
		renderPass.draw(3, 1, 0, 0);
	}
	m_renderPassContext->end(renderPass);
	if (auto *prof = m_context->frameProfiler())
		prof->endGpuScope("Pass.Composite", encoder);
	m_context->submitCommandEncoder(encoder, "CompositePass Commands");
}

void CompositePass::cleanup()
{
	m_bindGroupCache.clear();
	// Keep m_postBindGroup / m_postUniformBuffer alive - they don't depend on
	// the per-camera HDR target and survive resize.
}

std::shared_ptr<webgpu::WebGPUBindGroup> CompositePass::getOrCreateBindGroup(
	const std::shared_ptr<webgpu::WebGPUTexture> &texture,
	int layerIndex
)
{
	if (!texture)
		return nullptr;

	// ToDo: optimize cache key by using texture ID and layer index instead of pointer. Have a cachekey system for the engine
	auto cacheKey = reinterpret_cast<uint64_t>(texture.get()) ^ static_cast<uint64_t>(layerIndex);

	auto it = m_bindGroupCache.find(cacheKey);
	if (it != m_bindGroupCache.end())
		return it->second;

	auto bindGroupLayout = m_shaderInfo->getBindGroupLayout(4);
	if (!bindGroupLayout)
		return nullptr;

	std::vector<wgpu::BindGroupEntry> entries;
	entries.reserve(bindGroupLayout->getEntries().size());

	for (const auto &layoutEntry : bindGroupLayout->getEntries())
	{
		wgpu::BindGroupEntry entry{};
		entry.binding = layoutEntry.binding;

		if (layoutEntry.texture.sampleType != wgpu::TextureSampleType::Undefined)
			entry.textureView = texture->getTextureView(layerIndex);
		else if (layoutEntry.sampler.type != wgpu::SamplerBindingType::Undefined)
			entry.sampler = m_sampler;

		entries.push_back(entry);
	}

	wgpu::BindGroup rawBindGroup = m_context->bindGroupFactory().createBindGroup(
		bindGroupLayout->getLayout(),
		entries
	);

	auto bindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
		rawBindGroup,
		bindGroupLayout,
		std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{}
	);

	m_bindGroupCache[cacheKey] = bindGroup;
	return bindGroup;
}

} // namespace engine::rendering
