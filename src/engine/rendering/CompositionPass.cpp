#include "engine/rendering/CompositionPass.h"

#include <map>

#include <spdlog/spdlog.h>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameProfiler.h"
#include "engine/rendering/Mesh.h" // for engine::rendering::Topology
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/GBuffer.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering
{

namespace
{

constexpr const char *GBUFFER_BIND_GROUP_NAME = "GBuffer_BindGroup";

} // namespace

CompositionPass::CompositionPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(std::move(context))
{
}

bool CompositionPass::initialize()
{
	m_shader = m_context->shaderRegistry().getShader(shader::defaults::COMPOSITION_DEFERRED);
	if (!m_shader || !m_shader->isValid())
	{
		spdlog::error("CompositionPass: '{}' shader is missing or invalid", shader::defaults::COMPOSITION_DEFERRED);
		return false;
	}
	spdlog::info("CompositionPass initialized");
	return true;
}

void CompositionPass::cleanup()
{
	m_pipeline.reset();
	m_gBufferBindGroup.reset();
	m_gBufferBindGroupFingerprint = nullptr;
}

void CompositionPass::setGBuffer(webgpu::GBuffer *gBuffer)
{
	if (gBuffer != m_gBuffer)
	{
		m_gBufferBindGroup.reset();
		m_gBufferBindGroupFingerprint = nullptr;
	}
	m_gBuffer = gBuffer;
}

bool CompositionPass::ensureGBufferBindGroup()
{
	if (!m_gBuffer)
	{
		spdlog::error("CompositionPass: G-buffer not set");
		return false;
	}

	// Compare the first color texture's identity against the one used to build
	// the cached bind group. GBuffer::resize replaces every color texture, so a
	// pointer change here means the cached bind group is sampling destroyed views.
	const auto &textures = m_gBuffer->getColorTextures();
	const webgpu::WebGPUTexture *currentFingerprint =
		textures.empty() ? nullptr : textures[0].get();
	if (m_gBufferBindGroup && m_gBufferBindGroupFingerprint == currentFingerprint)
		return true;

	m_gBufferBindGroup.reset();

	auto layoutInfo = m_shader->getBindGroupLayout(GBUFFER_BIND_GROUP_NAME);
	if (!layoutInfo)
	{
		spdlog::error("CompositionPass: shader is missing '{}' layout", GBUFFER_BIND_GROUP_NAME);
		return false;
	}

	std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> overrides;
	for (uint32_t binding = 0; binding < textures.size(); ++binding)
	{
		// Group index is only used to disambiguate map keys here.
		overrides.emplace(
			std::make_tuple(uint32_t{0}, binding),
			webgpu::BindGroupResource(textures[binding])
		);
	}

	m_gBufferBindGroup = m_context->bindGroupFactory().createBindGroup(
		layoutInfo,
		overrides,
		nullptr,
		"CompositionPass.GBufferBindGroup"
	);

	if (!m_gBufferBindGroup)
	{
		spdlog::error("CompositionPass: failed to create G-buffer bind group");
		return false;
	}
	m_gBufferBindGroupFingerprint = currentFingerprint;
	return true;
}

bool CompositionPass::ensurePipeline()
{
	if (m_pipeline)
		return true;

	if (!m_renderPassContext || m_renderPassContext->getColorAttachmentCount() == 0)
	{
		spdlog::error("CompositionPass: render pass context missing a color attachment");
		return false;
	}

	auto targetTexture = m_renderPassContext->getColorTexture(0);
	if (!targetTexture)
	{
		spdlog::error("CompositionPass: target color texture is null");
		return false;
	}

	m_pipeline = m_context->pipelineManager().getOrCreatePipeline(
		m_shader,
		targetTexture->getFormat(),
		wgpu::TextureFormat::Undefined, // composition runs without depth test
		engine::rendering::Topology::Type::Triangles,
		wgpu::CullMode::None,
		false,
		1
	);

	if (!m_pipeline || !m_pipeline->isValid())
	{
		spdlog::error("CompositionPass: failed to create pipeline");
		m_pipeline.reset();
		return false;
	}
	return true;
}

void CompositionPass::render(FrameCache &frameCache)
{
	if (!m_shader)
	{
		spdlog::error("CompositionPass::render called before initialize()");
		return;
	}
	if (!m_renderPassContext)
	{
		spdlog::error("CompositionPass::render called without a render pass context");
		return;
	}
	if (!m_lightBindGroup || !m_shadowBindGroup || !m_clusterBindGroup || !m_environmentBindGroup)
	{
		spdlog::error("CompositionPass::render missing required bind groups (light/shadow/cluster/env)");
		return;
	}
	if (!ensureGBufferBindGroup())
		return;
	if (!ensurePipeline())
		return;

	// Frame bind group is held by FrameCache, keyed by camera. Resolve once
	// up-front so a missing entry fails fast instead of mid-render.
	auto frameIt = frameCache.frameBindGroupCache.find(m_cameraId);
	if (frameIt == frameCache.frameBindGroupCache.end() || !frameIt->second)
	{
		spdlog::error("CompositionPass: no frame bind group cached for camera {}", m_cameraId);
		return;
	}

	// One fullscreen triangle - no per-object state, so binding directly by
	// resolved name is cleaner than going through BindGroupBinder which is
	// optimised for many sequential draw calls.
	struct NamedGroup
	{
		const char *layoutName;
		std::shared_ptr<webgpu::WebGPUBindGroup> bindGroup;
	};
	const NamedGroup namedGroups[] = {
		{"GBuffer_BindGroup", m_gBufferBindGroup},
		{bindgroup::defaults::FRAME, frameIt->second},
		{bindgroup::defaults::LIGHT, m_lightBindGroup},
		{bindgroup::defaults::SHADOW, m_shadowBindGroup},
		{"ClusterGrid_BindGroup", m_clusterBindGroup},
		{bindgroup::defaults::ENVIRONMENT, m_environmentBindGroup},
	};

	auto encoder = m_context->createCommandEncoder("CompositionPass.Encoder");
	if (auto *prof = m_context->frameProfiler())
		prof->beginGpuScope("Pass.Composition", encoder);
	wgpu::RenderPassEncoder renderPass = m_renderPassContext->begin(encoder);
	renderPass.setPipeline(m_pipeline->getPipeline());

	for (const auto &slot : namedGroups)
	{
		auto idxOpt = m_shader->getBindGroupIndex(slot.layoutName);
		if (!idxOpt.has_value())
		{
			spdlog::error("CompositionPass: shader has no bind group '{}'", slot.layoutName);
			continue;
		}
		renderPass.setBindGroup(
			static_cast<uint32_t>(idxOpt.value()),
			slot.bindGroup->getBindGroup(),
			0,
			nullptr
		);
	}

	// One fullscreen triangle generated procedurally in the vertex shader.
	renderPass.draw(3, 1, 0, 0);

	m_renderPassContext->end(renderPass);
	if (auto *prof = m_context->frameProfiler())
		prof->endGpuScope("Pass.Composition", encoder);
	m_context->submitCommandEncoder(encoder, "CompositionPass.Commands");
}

} // namespace engine::rendering
