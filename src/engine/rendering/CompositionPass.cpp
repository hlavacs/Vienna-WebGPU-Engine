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
	m_shader = getValidatedShader(shader::defaults::COMPOSITION_DEFERRED);
	if (!m_shader)
		return false;
	spdlog::info("CompositionPass initialized");
	return true;
}

void CompositionPass::cleanup()
{
	m_pipeline = {};
	m_gBufferBindGroup.reset();
	m_gBufferBindGroupSignature.clear();
}

void CompositionPass::setGBuffer(webgpu::GBuffer *gBuffer)
{
	if (gBuffer != m_gBuffer)
	{
		m_gBufferBindGroup.reset();
		m_gBufferBindGroupSignature.clear();
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

	// Identity signature of every G-buffer texture. GBuffer::resize replaces
	// the whole array, so any pointer move in the vector means the cached
	// bind group is sampling destroyed views and must be rebuilt — even
	// when the owning GBuffer* is unchanged (the common case for in-frame
	// resizes like multi-camera split-screen with different viewports,
	// where Renderer::onResize never fires).
	const auto &textures = m_gBuffer->getColorTextures();
	engine::rendering::cache::BindGroupSignature signature;
	for (const auto &tex : textures) signature.add(tex);

	if (m_gBufferBindGroup && m_gBufferBindGroupSignature == signature)
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
	m_gBufferBindGroupSignature = std::move(signature);
	return true;
}

bool CompositionPass::ensurePipeline()
{
	if (m_pipeline.valid())
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

	auto pipelineSnapshot = m_pipeline.lock();
	if (!pipelineSnapshot || !pipelineSnapshot->isValid())
	{
		spdlog::error("CompositionPass: failed to create pipeline");
		m_pipeline = {};
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
	if (!m_sceneBindGroup)
	{
		spdlog::error("CompositionPass::render missing scene bind group");
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
		{bindgroup::defaults::FRAME, frameIt->second},
		{bindgroup::defaults::SCENE, m_sceneBindGroup},
		{"GBuffer_BindGroup",        m_gBufferBindGroup},
	};

	auto encoder = m_context->createCommandEncoder("CompositionPass.Encoder");
	if (auto *prof = m_context->frameProfiler())
		prof->beginGpuScope("Pass.Composition", encoder);
	// Pin the pipeline snapshot for the duration of this render pass — a
	// concurrent hot-reload swap inside the slot can't pull it out from
	// under the in-flight draws.
	auto pipelineSnapshot = m_pipeline.lock();
	if (!pipelineSnapshot)
	{
		spdlog::error("CompositionPass::render: pipeline handle is empty");
		return;
	}
	wgpu::RenderPassEncoder renderPass = m_renderPassContext->begin(encoder);
	renderPass.setPipeline(pipelineSnapshot->getPipeline());

	// Fill unused engine slots (Material@2, Object@3 — composition is a
	// fullscreen quad) with the shared empty bind group. wgpu requires every
	// pipeline-layout slot to have a bind group bound at draw time.
	auto emptyBg = m_context->pipelineManager().getOrCreateEmptyBindGroup();
	for (uint32_t slot = 0; slot < 4; ++slot)
		renderPass.setBindGroup(slot, emptyBg, 0, nullptr);

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
