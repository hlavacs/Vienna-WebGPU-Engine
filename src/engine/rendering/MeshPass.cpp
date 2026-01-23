#include "engine/rendering/MeshPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

namespace engine::rendering
{

MeshPass::MeshPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(context)
{
}

bool MeshPass::initialize()
{
	spdlog::info("Initializing MeshPass");

	// Get global bind group layouts
	m_frameBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("frameUniforms");
	if (!m_frameBindGroupLayout)
	{
		spdlog::error("Failed to get global bind group layout for frameUniforms");
		return false;
	}

	m_lightBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("lightUniforms");
	if (!m_lightBindGroupLayout)
	{
		spdlog::error("Failed to get global bind group layout for lightUniforms");
		return false;
	}

	m_objectBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("objectUniforms");
	if (!m_objectBindGroupLayout)
	{
		spdlog::error("Failed to get global bind group layout for objectUniforms");
		return false;
	}

	// Create light bind group
	m_lightBindGroup = m_context->bindGroupFactory().createBindGroup(m_lightBindGroupLayout);
	if (!m_lightBindGroup)
	{
		spdlog::error("Failed to create light bind group");
		return false;
	}

	spdlog::info("MeshPass initialized successfully");
	return true;
}

void MeshPass::render(FrameCache &frameCache)
{
	if (!m_renderPassContext)
	{
		spdlog::error("MeshPass::render() called without setting render pass context");
		return;
	}

	spdlog::debug("MeshPass::render() - visibleIndices count: {}, gpuItems count: {}", m_visibleIndices.size(), frameCache.gpuRenderItems.size());

	// Update lights from frame cache
	updateLights(frameCache.lightUniforms);

	// Create command encoder
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "MeshPass Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// Begin render pass using context's begin() method
	wgpu::RenderPassEncoder renderPass = m_renderPassContext->begin(encoder);

	// Bind frame and light uniforms
	bindFrameUniforms(renderPass, m_cameraId, m_frameUniforms);
	bindLightUniforms(renderPass);

	// Draw items from frame cache
	drawItems(encoder, renderPass, m_renderPassContext, frameCache.gpuRenderItems, m_visibleIndices);

	// End render pass using context's end() method
	m_renderPassContext->end(renderPass);

	// Submit commands
	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = "MeshPass Commands";
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
	encoder.release();
	m_context->getQueue().submit(commands);
	commands.release();
}

void MeshPass::bindFrameUniforms(
	wgpu::RenderPassEncoder renderPass,
	uint64_t cameraId,
	const FrameUniforms &frameUniforms
)
{
	auto &frameBindGroup = m_frameBindGroupCache[cameraId];
	if (!frameBindGroup)
	{
		frameBindGroup = m_context->bindGroupFactory().createBindGroup(m_frameBindGroupLayout);
		spdlog::info("Created new frame bind group for camera {}", cameraId);
	}

	frameBindGroup->updateBuffer(0, &frameUniforms, sizeof(FrameUniforms), 0, m_context->getQueue());
	renderPass.setBindGroup(0, frameBindGroup->getBindGroup(), 0, nullptr);
}

void MeshPass::bindLightUniforms(wgpu::RenderPassEncoder renderPass)
{
	renderPass.setBindGroup(1, m_lightBindGroup->getBindGroup(), 0, nullptr);
}

void MeshPass::updateLights(const std::vector<LightStruct> &lights)
{
	// Always write the header, even if there are no lights (count = 0)
	LightsBuffer header;
	header.count = static_cast<uint32_t>(lights.size());

	// Write header to the global lights buffer at offset 0
	m_lightBindGroup->updateBuffer(0, &header, sizeof(LightsBuffer), 0, m_context->getQueue());

	// Write light data if any lights exist at offset sizeof(LightsBuffer)
	if (!lights.empty())
	{
		m_lightBindGroup->updateBuffer(0, lights.data(), lights.size() * sizeof(LightStruct), sizeof(LightsBuffer), m_context->getQueue());
	}
}

void MeshPass::drawItems(
	wgpu::CommandEncoder &encoder,
	wgpu::RenderPassEncoder renderPass,
	const std::shared_ptr<webgpu::WebGPURenderPassContext> &renderPassContext,
	const std::vector<std::optional<RenderItemGPU>> &gpuItems,
	const std::vector<size_t> &indicesToRender
)
{
	webgpu::WebGPUPipeline *currentPipeline = nullptr;
	webgpu::WebGPUMesh *currentMesh = nullptr;

	spdlog::debug("MeshPass::drawItems() - Rendering {} items", indicesToRender.size());

	size_t itemsRendered = 0;
	size_t itemsSkipped = 0;

	for (uint32_t index : indicesToRender)
	{
		if (index >= gpuItems.size())
		{
			spdlog::warn("Index {} out of bounds (gpuItems.size = {})", index, gpuItems.size());
			continue;
		}

		const auto &optionalItem = gpuItems[index];
		if (!optionalItem.has_value())
		{
			itemsSkipped++;
			continue;
		}

		const RenderItemGPU &item = optionalItem.value();
		if (!item.gpuMesh || !item.gpuMaterial || !item.objectBindGroup)
		{
			spdlog::warn("Missing GPU resources - mesh: {}, material: {}, bindGroup: {}", item.gpuMesh != nullptr, item.gpuMaterial != nullptr, item.objectBindGroup != nullptr);
			itemsSkipped++;
			continue;
		}

		auto meshPtr = item.gpuMesh->getCPUHandle().get();
		auto materialPtr = item.gpuMaterial->getCPUHandle().get();

		if (!meshPtr.has_value() || !materialPtr.has_value())
		{
			spdlog::warn("Invalid CPU handles - mesh: {}, material: {}", meshPtr.has_value(), materialPtr.has_value());
			itemsSkipped++;
			continue;
		}

		auto pipeline = m_context->pipelineManager().getOrCreatePipeline(
			meshPtr.value(),
			materialPtr.value(),
			renderPassContext
		);

		if (!pipeline || !pipeline->isValid())
		{
			spdlog::warn("Invalid pipeline for mesh/material");
			itemsSkipped++;
			continue;
		}

		// Bind pipeline only when changed
		if (pipeline.get() != currentPipeline)
		{
			currentPipeline = pipeline.get();
			currentMesh = nullptr;
			renderPass.setPipeline(currentPipeline->getPipeline());
		}

		// Bind vertex/index buffers only when mesh changes
		if (item.gpuMesh != currentMesh)
		{
			currentMesh = item.gpuMesh;
			currentMesh->bindBuffers(
				renderPass,
				currentPipeline->getVertexLayout()
			);
		}
		// Bind object bind group (group 2)
		renderPass.setBindGroup(2, item.objectBindGroup->getBindGroup(), 0, nullptr);

		// Bind material (group 3)
		item.gpuMaterial->bind(renderPass);

		// Bind shadow bind group (group 4)
		if (m_shadowBindGroup)
		{
			renderPass.setBindGroup(4, m_shadowBindGroup->getBindGroup(), 0, nullptr);
		}

		// Draw submesh
		item.gpuMesh->isIndexed()
			? renderPass.drawIndexed(item.submesh.indexCount, 1, item.submesh.indexOffset, 0, 0)
			: renderPass.draw(item.submesh.indexCount, 1, item.submesh.indexOffset, 0);

		itemsRendered++;
	}

	spdlog::debug("MeshPass::drawItems() - Rendered: {}, Skipped: {}", itemsRendered, itemsSkipped);
}

void MeshPass::cleanup()
{
	m_frameBindGroupCache.clear();
}

} // namespace engine::rendering
