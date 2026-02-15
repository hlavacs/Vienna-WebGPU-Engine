#include "engine/rendering/MeshPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupBinder.h"
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

	updateLightUniforms(frameCache);

	// Create command encoder
	auto encoder = m_context->createCommandEncoder("MeshPass Encoder");

	// Begin render pass using context's begin() method
	wgpu::RenderPassEncoder renderPass = m_renderPassContext->begin(encoder);
	{
		// Draw items from frame cache
		drawItems(encoder, renderPass, frameCache, frameCache.gpuRenderItems, m_visibleIndices);
	}
	m_renderPassContext->end(renderPass);

	m_context->submitCommandEncoder(encoder, "MeshPass Commands");
}

bool MeshPass::updateLightUniforms(FrameCache &frameCache)
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
	FrameCache &frameCache,
	const std::vector<std::optional<RenderItemGPU>> &gpuItems,
	const std::vector<size_t> &indicesToRender
)
{
	std::shared_ptr<webgpu::WebGPUPipeline> currentPipeline = nullptr;
	webgpu::WebGPUMesh *currentMesh = nullptr;
	webgpu::WebGPUMaterial *currentMaterial = nullptr;

	// Create bind group binder helper
	BindGroupBinder binder(&frameCache);

	spdlog::debug("MeshPass::drawItems() - Rendering {} items", indicesToRender.size());

	size_t itemsRendered = 0;
	size_t itemsSkipped = 0;

	for (const auto &index : indicesToRender)
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

		const auto &item = optionalItem.value();
		if (!item.gpuMesh || !item.gpuMaterial || !item.objectBindGroup)
		{
			spdlog::warn("Missing GPU resources - mesh: {}, material: {}, bindGroup: {}", item.gpuMesh != nullptr, item.gpuMaterial != nullptr, item.objectBindGroup != nullptr);
			itemsSkipped++;
			continue;
		}

		// Check if pipeline needs to change (mesh or material changed)
		bool pipelineChanged = (item.gpuMesh != currentMesh || item.gpuMaterial.get() != currentMaterial);

		if (pipelineChanged)
		{
			auto cpuMesh = item.gpuMesh->getCPUHandle().get();
			auto cpuMaterial = item.gpuMaterial->getCPUHandle().get();

			if (!cpuMesh.has_value() || !cpuMaterial.has_value())
			{
				spdlog::warn("Invalid CPU handles - mesh: {}, material: {}", cpuMesh.has_value(), cpuMaterial.has_value());
				itemsSkipped++;
				continue;
			}

			currentPipeline = m_context->pipelineManager().getOrCreatePipeline(
				cpuMesh.value(),
				cpuMaterial.value(),
				m_renderPassContext
			);

			if (!currentPipeline || !currentPipeline->isValid())
			{
				spdlog::warn("Invalid pipeline for mesh/material");
				itemsSkipped++;
				continue;
			}
			renderPass.setPipeline(currentPipeline->getPipeline());

			currentMaterial = item.gpuMaterial.get();
		}

		// Bind all shader groups - binder tracks what's already bound and skips redundant binds
		// This handles per-object bind groups that change even with the same mesh/material
		if (currentPipeline && currentPipeline->getShaderInfo())
		{
			// Extract material ID from the material pointer for PerMaterial bind group tracking
			uint64_t materialId = reinterpret_cast<uint64_t>(item.gpuMaterial.get());

			binder.bind(
				renderPass,
				currentPipeline,
				m_cameraId,
				{{BindGroupType::Object, item.objectBindGroup},
				 {BindGroupType::Material, item.gpuMaterial->getBindGroup()},
				 {BindGroupType::Light, m_lightBindGroup},
				 {BindGroupType::Shadow, m_shadowBindGroup}},
				item.objectID, // objectId for PerObject custom bind groups
				materialId	  // materialId for PerMaterial custom bind groups
			);
		}

		// Bind vertex/index buffers only when mesh changes
		if (item.gpuMesh != currentMesh)
		{
			currentMesh = item.gpuMesh;
			currentMesh->bindBuffers(renderPass, currentPipeline->getVertexLayout());
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
}

} // namespace engine::rendering
