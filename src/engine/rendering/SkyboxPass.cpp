#include "engine/rendering/SkyboxPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/FrameCache.h"
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
	m_shaderInfo = m_context->shaderRegistry().getShader(shader::defaults::SKYBOX);
	if (!m_shaderInfo || !m_shaderInfo->isValid())
	{
		spdlog::error("Skybox shader not found in registry");
		return false;
	}

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

	auto pipeline = m_pipeline.lock();
	if (!pipeline)
	{
		m_pipeline = m_context->pipelineManager().getOrCreatePipeline(
			m_shaderInfo,
			colorTexture->getFormat(),
			wgpu::TextureFormat::Undefined,
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
	auto pass = m_renderPassContext->begin(encoder);
	pass.setPipeline(pipeline->getPipeline());
	pass.setBindGroup(0, frameBindGroupIt->second->getBindGroup(), 0, nullptr);
	pass.setBindGroup(1, m_environmentBindGroup->getBindGroup(), 0, nullptr);
	pass.draw(36, 1, 0, 0);
	m_renderPassContext->end(pass);
	m_context->submitCommandEncoder(encoder, "SkyboxPass Commands");
}

void SkyboxPass::cleanup()
{
}

} // namespace engine::rendering
