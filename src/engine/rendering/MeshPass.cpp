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
	m_lightBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout(bindgroup::defaults::LIGHT);
	if (!m_lightBindGroupLayout)
	{
		spdlog::error("Failed to get global bind group layout for lightUniforms");
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

	// Create command encoder
	auto encoder = m_context->createCommandEncoder("MeshPass Encoder");

	// Begin render pass using context's begin() method
	wgpu::RenderPassEncoder renderPass = m_renderPassContext->begin(encoder);
	{
		// Bind frame and light uniforms
		bindFrameUniforms(renderPass, frameCache);
		bindLightUniforms(renderPass, frameCache);

		// Draw items from frame cache
		drawItems(encoder, renderPass, frameCache.gpuRenderItems, m_visibleIndices);
	}
	m_renderPassContext->end(renderPass);

	m_context->submitCommandEncoder(encoder, "MeshPass Commands");
}

bool MeshPass::bindFrameUniforms(wgpu::RenderPassEncoder renderPass, FrameCache &frameCache)
{
	auto frameBindGroup = frameCache.frameBindGroupCache[m_cameraId];
	if (!frameBindGroup)
	{
		spdlog::error("Frame bind group not found in cache for camera ID {}", m_cameraId);
		return false;
	}
	renderPass.setBindGroup(0, frameBindGroup->getBindGroup(), 0, nullptr);
	return true;
}

bool MeshPass::bindLightUniforms(wgpu::RenderPassEncoder renderPass, FrameCache &frameCache)
{
	const auto &lights = frameCache.lightUniforms;
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
	renderPass.setBindGroup(1, m_lightBindGroup->getBindGroup(), 0, nullptr);
	return true;
}

bool MeshPass::bindObjectUniforms(
	wgpu::RenderPassEncoder renderPass,
	const std::shared_ptr<webgpu::WebGPUShaderInfo> &webgpuShaderInfo,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &objectBindGroup
)
{
	if (RenderPass::bind(renderPass, webgpuShaderInfo, objectBindGroup))
	{
		return true;
	}
	else
	{
		spdlog::error("Failed to bind object uniforms");
		return false;
	}
}

void MeshPass::drawItems(
	wgpu::CommandEncoder &encoder,
	wgpu::RenderPassEncoder renderPass,
	const std::vector<std::optional<RenderItemGPU>> &gpuItems,
	const std::vector<size_t> &indicesToRender
)
{
	std::shared_ptr<webgpu::WebGPUPipeline> currentPipeline = nullptr;
	webgpu::WebGPUMesh *currentMesh = nullptr;
	webgpu::WebGPUMaterial *currentMaterial = nullptr;

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

		if (item.gpuMesh != currentMesh || item.gpuMaterial.get() != currentMaterial)
		{
			auto meshPtr = item.gpuMesh->getCPUHandle().get();
			auto materialPtr = item.gpuMaterial->getCPUHandle().get();

			if (!meshPtr.has_value() || !materialPtr.has_value())
			{
				spdlog::warn("Invalid CPU handles - mesh: {}, material: {}", meshPtr.has_value(), materialPtr.has_value());
				itemsSkipped++;
				continue;
			}

			currentPipeline = m_context->pipelineManager().getOrCreatePipeline(
				meshPtr.value(),
				materialPtr.value(),
				m_renderPassContext
			);

			if (!currentPipeline || !currentPipeline->isValid())
			{
				spdlog::warn("Invalid pipeline for mesh/material");
				itemsSkipped++;
				continue;
			}
			renderPass.setPipeline(currentPipeline->getPipeline());

			if (item.gpuMaterial.get() != currentMaterial)
			{
				currentMaterial = item.gpuMaterial.get();
				RenderPass::bind(renderPass, currentPipeline->getShaderInfo(), item.gpuMaterial->getBindGroup());
			}
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
		bindObjectUniforms(renderPass, currentPipeline->getShaderInfo(), item.objectBindGroup);

		if (m_shadowBindGroup)
		{
			RenderPass::bind(renderPass, currentPipeline->getShaderInfo(), m_shadowBindGroup);
		}

		// ToDo: Bind other global bind groups as needed

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
}

} // namespace engine::rendering
