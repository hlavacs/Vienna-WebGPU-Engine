#include "engine/rendering/SkyboxPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameProfiler.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

namespace engine::rendering
{

SkyboxPass::SkyboxPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(context)
{
}

bool SkyboxPass::initialize()
{
	m_shaderInfo = getValidatedShader(shader::defaults::SKYBOX);
	if (!m_shaderInfo)
		return false;

	return true;
}

void SkyboxPass::render(FrameCache &frameCache)
{
	if (!m_renderPassContext || !m_environmentBindGroup)
	{
		return;
	}

	auto frameBindGroupIt = frameCache.frameBindGroupCache.find(m_cameraId);
	if (frameBindGroupIt == frameCache.frameBindGroupCache.end() || !frameBindGroupIt->second)
	{
		spdlog::warn("SkyboxPass skipped: missing frame bind group for camera {}", m_cameraId);
		return;
	}

	auto colorTexture = m_renderPassContext->getColorTexture(0);
	if (!colorTexture)
	{
		spdlog::warn("SkyboxPass skipped: missing color attachment");
		return;
	}

	// Pick up the depth format from whatever depth attachment the renderer
	// handed us. When run as a deferred-shading background pass this is the
	// G-buffer's depth (Depth32Float); when run in forward setups it can be
	// the per-camera depth buffer instead - both work because the depth
	// compare/write state was baked into the shader info at registration.
	auto depthTexture = m_renderPassContext->getDepthTexture();
	const wgpu::TextureFormat depthFormat = depthTexture
		? depthTexture->getFormat()
		: wgpu::TextureFormat::Undefined;

	auto pipeline = m_pipeline.lock();
	if (!pipeline)
	{
		m_pipeline = m_context->pipelineManager().getOrCreatePipeline(
			m_shaderInfo,
			colorTexture->getFormat(),
			depthFormat,
			Topology::Triangles,
			wgpu::CullMode::None,
			false,
			1
		);
		pipeline = m_pipeline.lock();
	}

	if (!pipeline || !pipeline->isValid())
	{
		spdlog::warn("SkyboxPass skipped: failed to create pipeline");
		return;
	}

	auto encoder = m_context->createCommandEncoder("SkyboxPass Encoder");
	if (auto *prof = m_context->frameProfiler())
		prof->beginGpuScope("Pass.Skybox", encoder);
	auto pass = m_renderPassContext->begin(encoder);
	pass.setPipeline(pipeline->getPipeline());
	// Fill Scene/Material/Object slots with the shared empty bind group; skybox
	// only samples Frame@0 and its own custom env at @4. wgpu validates every
	// declared pipeline-layout slot at draw time.
	auto emptyBg = m_context->pipelineManager().getOrCreateEmptyBindGroup();
	pass.setBindGroup(0, frameBindGroupIt->second->getBindGroup(), 0, nullptr);
	for (uint32_t slot = 1; slot < 4; ++slot)
		pass.setBindGroup(slot, emptyBg, 0, nullptr);
	pass.setBindGroup(4, m_environmentBindGroup->getBindGroup(), 0, nullptr);
	pass.draw(36, 1, 0, 0);
	m_renderPassContext->end(pass);
	if (auto *prof = m_context->frameProfiler())
		prof->endGpuScope("Pass.Skybox", encoder);
	m_context->submitCommandEncoder(encoder, "SkyboxPass Commands");
}

void SkyboxPass::cleanup()
{
}

} // namespace engine::rendering
