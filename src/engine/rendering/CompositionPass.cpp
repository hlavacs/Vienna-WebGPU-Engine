#include "engine/rendering/CompositionPass.h"

#include <array>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/ShadowPass.h"
#include "engine/rendering/webgpu/GBuffer.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering
{

CompositionPass::CompositionPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	m_context(context)
{
}

bool CompositionPass::initialize()
{
	spdlog::info("Initializing CompositionPass");

	if (!m_context)
	{
		spdlog::error("WebGPUContext is null");
		return false;
	}

	auto device = m_context->getDevice();

	// Load deferred composition shader
	auto shaderPath = engine::core::PathProvider::getResource("deferred_composition.wgsl");
	spdlog::info("Loading deferred composition shader from: {}", shaderPath.string());

	auto shaderModule = m_context->shaderFactory().loadShaderModule(shaderPath);
	if (!shaderModule)
	{
		spdlog::error("Failed to load deferred composition shader");
		return false;
	}

	// Create render pipeline with composition shader
	wgpu::RenderPipelineDescriptor pipelineDesc{};
	pipelineDesc.label = "DeferredComposition_Pipeline";
	pipelineDesc.layout = nullptr;  // auto layout

	// Vertex stage
	wgpu::VertexState vertexState{};
	vertexState.module = shaderModule;
	vertexState.entryPoint = "vs_main";
	vertexState.bufferCount = 0;
	vertexState.buffers = nullptr;

	pipelineDesc.vertex = vertexState;

	// Fragment stage
	wgpu::ColorTargetState colorTarget{};
	colorTarget.format = wgpu::TextureFormat::RGBA16Float; // HDR intermediate target format
	colorTarget.blend = nullptr;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;

	wgpu::FragmentState fragmentState{};
	fragmentState.module = shaderModule;
	fragmentState.entryPoint = "fs_main";
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;

	pipelineDesc.fragment = &fragmentState;

	// Primitive state
	pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
	pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
	pipelineDesc.primitive.cullMode = wgpu::CullMode::None;

	// Depth/stencil (disabled for composition)
	pipelineDesc.depthStencil = nullptr;

	// Multisample
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = 0xFFFFFFFF;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	auto pipeline = device.createRenderPipeline(pipelineDesc);
	if (!pipeline)
	{
		spdlog::error("Failed to create deferred composition pipeline");
		return false;
	}

	m_pipeline = pipeline;

	// Note: Full-screen quad generation is done in the vertex shader
	// (single full-screen triangle)
	m_fullScreenQuadVertexCount = 3;

	spdlog::info("CompositionPass initialized successfully");
	return true;
}

bool CompositionPass::render(
	FrameCache &frameCache,
	const std::shared_ptr<webgpu::GBuffer> &gBuffer,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &sceneLightBindGroup,
	const ShadowPass &shadowPass,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &clusterBindGroup
)
{
	spdlog::debug("CompositionPass::render() called");

	// Validate all inputs
	if (!m_context || !gBuffer || !sceneLightBindGroup || !clusterBindGroup)
	{
		spdlog::error("CompositionPass::render - Null input parameters");
		return false;
	}

	if (!m_pipeline)
	{
		spdlog::error("CompositionPass::render - Pipeline not initialized");
		return false;
	}

	if (frameCache.renderTargets.empty())
	{
		spdlog::error("CompositionPass::render - No render targets in frame cache");
		return false;
	}

	// Get the first (or primary) render target from frame cache
	const auto &renderTarget = frameCache.renderTargets.begin()->second;
	if (!renderTarget.gpuTexture)
	{
		spdlog::error("CompositionPass::render - Render target has no GPU texture");
		return false;
	}

	// Get G-buffer individual textures
	auto positionTexture = gBuffer->getPositionTexture();
	auto normalTexture = gBuffer->getNormalTexture();
	auto albedoTexture = gBuffer->getAlbedoTexture();
	auto materialTexture = gBuffer->getMaterialTexture();

	if (!positionTexture || !normalTexture || !albedoTexture || !materialTexture)
	{
		spdlog::error("CompositionPass::render - One or more G-buffer textures are invalid");
		return false;
	}

	auto device = m_context->getDevice();
	auto queue = m_context->getQueue();

	// Create render pass descriptor
	wgpu::RenderPassColorAttachment colorAttachment{};
	colorAttachment.view = renderTarget.gpuTexture->getTextureView();
	colorAttachment.clearValue = {0.0f, 0.0f, 0.0f, 1.0f};
	colorAttachment.loadOp = wgpu::LoadOp::Clear;
	colorAttachment.storeOp = wgpu::StoreOp::Store;

	wgpu::RenderPassDescriptor renderPassDesc{};
	renderPassDesc.label = "CompositionPass_RenderPass";
	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &colorAttachment;
	renderPassDesc.depthStencilAttachment = nullptr;

	// Create command encoder
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "CompositionPass_CommandEncoder";
	auto commandEncoder = device.createCommandEncoder(encoderDesc);

	// Begin render pass
	auto renderPass = commandEncoder.beginRenderPass(renderPassDesc);
	renderPass.setPipeline(m_pipeline);

	// ===== GROUP 0: G-Buffers (4 textures) =====

	// Use pipeline implicit layout (pipeline was created with layout = nullptr).
	auto gBufferLayout = m_pipeline.getBindGroupLayout(0);

	// Create bind group entries
	std::vector<wgpu::BindGroupEntry> gBufferEntries(4);
	gBufferEntries[0].binding = 0;
	gBufferEntries[0].textureView = positionTexture->getTextureView();
	gBufferEntries[1].binding = 1;
	gBufferEntries[1].textureView = normalTexture->getTextureView();
	gBufferEntries[2].binding = 2;
	gBufferEntries[2].textureView = albedoTexture->getTextureView();
	gBufferEntries[3].binding = 3;
	gBufferEntries[3].textureView = materialTexture->getTextureView();

	wgpu::BindGroupDescriptor gBufferDesc{};
	gBufferDesc.layout = gBufferLayout;
	gBufferDesc.entryCount = gBufferEntries.size();
	gBufferDesc.entries = gBufferEntries.data();
	gBufferDesc.label = "GBufferBindGroup";

	auto gBufferBindGroup = device.createBindGroup(gBufferDesc);
	if (!gBufferBindGroup)
	{
		spdlog::error("Failed to create G-buffer bind group");
		renderPass.end();
		commandEncoder.release();
		return false;
	}

	renderPass.setBindGroup(0, gBufferBindGroup, 0, nullptr);

	// ===== GROUP 1: Frame Uniforms =====
	if (!frameCache.frameBindGroupCache.empty())
	{
		auto frameBindGroup = frameCache.frameBindGroupCache.begin()->second;
		if (frameBindGroup)
		{
			auto frameLayout = m_pipeline.getBindGroupLayout(1);
			auto frameBuffer = frameBindGroup->findBufferByBinding(0);
			if (frameBuffer)
			{
				wgpu::BindGroupEntry frameEntry{};
				frameEntry.binding = 0;
				frameEntry.buffer = frameBuffer->getBuffer();
				frameEntry.offset = 0;
				frameEntry.size = frameBuffer->getSize();

				wgpu::BindGroupDescriptor frameDesc{};
				frameDesc.layout = frameLayout;
				frameDesc.entryCount = 1;
				frameDesc.entries = &frameEntry;
				frameDesc.label = "Composition_FrameBindGroup";

				auto frameImplicitBindGroup = device.createBindGroup(frameDesc);
				renderPass.setBindGroup(1, frameImplicitBindGroup, 0, nullptr);
			}
		}
	}

	// ===== GROUP 2: Light Data =====
	{
		auto lightLayout = m_pipeline.getBindGroupLayout(2);
		auto lightBuffer = sceneLightBindGroup->findBufferByBinding(0);
		if (!lightBuffer)
		{
			spdlog::error("CompositionPass::render - Missing light buffer at binding 0");
			renderPass.end();
			commandEncoder.release();
			return false;
		}

		wgpu::BindGroupEntry lightEntry{};
		lightEntry.binding = 0;
		lightEntry.buffer = lightBuffer->getBuffer();
		lightEntry.offset = 0;
		lightEntry.size = lightBuffer->getSize();

		wgpu::BindGroupDescriptor lightDesc{};
		lightDesc.layout = lightLayout;
		lightDesc.entryCount = 1;
		lightDesc.entries = &lightEntry;
		lightDesc.label = "Composition_LightBindGroup";

		auto lightImplicitBindGroup = device.createBindGroup(lightDesc);
		renderPass.setBindGroup(2, lightImplicitBindGroup, 0, nullptr);
	}

	// ===== GROUP 3: Shadow Maps =====
	{
		auto shadowSampler = shadowPass.getShadowSampler();
		auto shadow2DArray = shadowPass.getShadow2DArray();
		auto shadowCubeArray = shadowPass.getShadowCubeArray();
		auto shadowStorageBuffer = shadowPass.getShadowBindGroup() ? shadowPass.getShadowBindGroup()->findBufferByBinding(3) : nullptr;

		if (!shadowSampler || !shadow2DArray || !shadowCubeArray || !shadowStorageBuffer)
		{
			spdlog::error("CompositionPass::render - Shadow resources are invalid");
			renderPass.end();
			commandEncoder.release();
			return false;
		}

		std::array<wgpu::BindGroupEntry, 4> shadowEntries{};
		shadowEntries[0].binding = 0;
		shadowEntries[0].sampler = shadowSampler;
		shadowEntries[1].binding = 1;
		shadowEntries[1].textureView = shadow2DArray->getTextureView();
		shadowEntries[2].binding = 2;
		shadowEntries[2].textureView = shadowCubeArray->getTextureView();
		shadowEntries[3].binding = 3;
		shadowEntries[3].buffer = shadowStorageBuffer->getBuffer();
		shadowEntries[3].offset = 0;
		shadowEntries[3].size = shadowStorageBuffer->getSize();

		wgpu::BindGroupDescriptor shadowDesc{};
		shadowDesc.layout = m_pipeline.getBindGroupLayout(3);
		shadowDesc.entryCount = static_cast<uint32_t>(shadowEntries.size());
		shadowDesc.entries = shadowEntries.data();
		shadowDesc.label = "Composition_ShadowBindGroup";

		auto shadowImplicitBindGroup = device.createBindGroup(shadowDesc);
		if (!shadowImplicitBindGroup)
		{
			spdlog::error("CompositionPass::render - Failed to create shadow bind group");
			renderPass.end();
			commandEncoder.release();
			return false;
		}

		renderPass.setBindGroup(3, shadowImplicitBindGroup, 0, nullptr);
	}

	// ===== GROUP 4: Cluster Grid =====
	{
		auto clusterLayout = m_pipeline.getBindGroupLayout(4);
		auto clusterGridBuffer = clusterBindGroup->findBufferByBinding(0);
		auto clusterIndicesBuffer = clusterBindGroup->findBufferByBinding(1);
		if (!clusterGridBuffer || !clusterIndicesBuffer)
		{
			spdlog::error("CompositionPass::render - Missing cluster buffers at bindings 0/1");
			renderPass.end();
			commandEncoder.release();
			return false;
		}

		std::array<wgpu::BindGroupEntry, 2> clusterEntries{};
		clusterEntries[0].binding = 0;
		clusterEntries[0].buffer = clusterGridBuffer->getBuffer();
		clusterEntries[0].offset = 0;
		clusterEntries[0].size = clusterGridBuffer->getSize();
		clusterEntries[1].binding = 1;
		clusterEntries[1].buffer = clusterIndicesBuffer->getBuffer();
		clusterEntries[1].offset = 0;
		clusterEntries[1].size = clusterIndicesBuffer->getSize();

		wgpu::BindGroupDescriptor clusterDesc{};
		clusterDesc.layout = clusterLayout;
		clusterDesc.entryCount = static_cast<uint32_t>(clusterEntries.size());
		clusterDesc.entries = clusterEntries.data();
		clusterDesc.label = "Composition_ClusterBindGroup";

		auto clusterImplicitBindGroup = device.createBindGroup(clusterDesc);
		renderPass.setBindGroup(4, clusterImplicitBindGroup, 0, nullptr);
	}

	// Draw fullscreen triangle
	renderPass.draw(m_fullScreenQuadVertexCount, 1, 0, 0);

	renderPass.end();

	// Finish and submit command buffer
	auto commandBuffer = commandEncoder.finish();
	queue.submit(1, &commandBuffer);

	spdlog::debug("CompositionPass::render completed successfully");
	return true;
}

} // namespace engine::rendering
