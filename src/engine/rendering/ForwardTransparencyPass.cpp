#include "engine/rendering/ForwardTransparencyPass.h"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupBinder.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MaterialFeatureMask.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/ShaderRegistry.h"
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

ForwardTransparencyPass::ForwardTransparencyPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(std::move(context))
{
}

bool ForwardTransparencyPass::initialize()
{
	m_shaderInfo = m_context->shaderRegistry().getShader(shader::defaults::PBR);
	if (!m_shaderInfo || !m_shaderInfo->isValid())
	{
		spdlog::error("ForwardTransparencyPass: PBR shader '{}' is missing or invalid",
			shader::defaults::PBR);
		return false;
	}
	spdlog::info("ForwardTransparencyPass initialized");
	return true;
}

void ForwardTransparencyPass::cleanup()
{
}

void ForwardTransparencyPass::render(FrameCache &frameCache)
{
	if (!m_renderPassContext)
	{
		spdlog::warn("ForwardTransparencyPass::render called without a render-pass context");
		return;
	}
	if (!m_shaderInfo)
	{
		spdlog::error("ForwardTransparencyPass::render called before initialize()");
		return;
	}

	// Skip rather than crash if the deferred path's shared bind groups failed
	// to bind this frame - the lit HDR target is already valid.
	if (!m_lightBindGroup || !m_shadowBindGroup || !m_environmentBindGroup)
	{
		spdlog::debug("ForwardTransparencyPass skipped: missing light/shadow/env bind group");
		return;
	}

	struct DrawCandidate
	{
		size_t index;
		float distanceSq;
	};
	std::vector<DrawCandidate> candidates;
	candidates.reserve(m_visibleIndices.size() / 8); // typical scenes are mostly opaque

	const auto &gpuItems = frameCache.gpuRenderItems;
	for (size_t idx : m_visibleIndices)
	{
		if (idx >= gpuItems.size() || !gpuItems[idx].has_value())
			continue;

		const auto &item = gpuItems[idx].value();
		if (!item.gpuMesh || !item.gpuMaterial || !item.objectBindGroup)
			continue;

		auto cpuMatOpt = item.gpuMaterial->getCPUHandle().get();
		if (!cpuMatOpt.has_value())
			continue;

		const auto features = cpuMatOpt.value()->getFeatureMask();
		if (!MaterialFeature::hasFlag(features, MaterialFeature::Flag::Transparent))
			continue;

		// Use object position from the world transform's translation row as the
		// sort key. Per-submesh AABB centre would be slightly more accurate for
		// large meshes but requires an extra cache lookup; object-origin sorting
		// is what the global RenderCollector::sort already uses.
		const glm::vec3 itemPos = glm::vec3(item.worldTransform[3]);
		const glm::vec3 diff = itemPos - m_cameraPosition;
		candidates.push_back({idx, glm::dot(diff, diff)});
	}

	if (candidates.empty())
	{
		// Nothing to draw - skip the encoder + render-pass overhead entirely.
		return;
	}

	// Back-to-front: largest distance first so closer surfaces blend on top.
	std::sort(
		candidates.begin(),
		candidates.end(),
		[](const DrawCandidate &a, const DrawCandidate &b) { return a.distanceSq > b.distanceSq; }
	);

	auto encoder = m_context->createCommandEncoder("ForwardTransparencyPass.Encoder");
	wgpu::RenderPassEncoder renderPass = m_renderPassContext->begin(encoder);

	BindGroupBinder binder(&frameCache);
	std::shared_ptr<webgpu::WebGPUPipeline> currentPipeline;
	webgpu::WebGPUMesh *currentMesh = nullptr;
	webgpu::WebGPUMaterial *currentMaterial = nullptr;
	size_t drawn = 0;
	size_t skipped = 0;

	for (const auto &candidate : candidates)
	{
		const auto &item = gpuItems[candidate.index].value();

		const bool meshOrMaterialChanged = (item.gpuMesh != currentMesh
			|| item.gpuMaterial.get() != currentMaterial);

		if (meshOrMaterialChanged)
		{
			auto cpuMeshOpt = item.gpuMesh->getCPUHandle().get();
			auto cpuMaterialOpt = item.gpuMaterial->getCPUHandle().get();
			if (!cpuMeshOpt.has_value() || !cpuMaterialOpt.has_value())
			{
				++skipped;
				continue;
			}

			// pipelineManager pulls blend-enabled + depth-write-off from the
			// material's Transparent flag (see WebGPUPipelineFactory::createRenderPipeline).
			auto pipeline = m_context->pipelineManager().getOrCreatePipeline(
				cpuMeshOpt.value(),
				cpuMaterialOpt.value(),
				m_renderPassContext
			);
			if (!pipeline || !pipeline->isValid())
			{
				++skipped;
				continue;
			}

			if (pipeline != currentPipeline)
			{
				currentPipeline = pipeline;
				renderPass.setPipeline(currentPipeline->getPipeline());
				// New pipeline can invalidate vertex-buffer binding.
				currentMesh = nullptr;
			}
			currentMaterial = item.gpuMaterial.get();
		}

		if (!currentPipeline || !currentPipeline->getShaderInfo())
		{
			++skipped;
			continue;
		}

		const uint64_t materialId = reinterpret_cast<uint64_t>(item.gpuMaterial.get());
		binder.bind(
			renderPass,
			currentPipeline,
			m_cameraId,
			{
				{BindGroupType::Object, item.objectBindGroup},
				{BindGroupType::Material, item.gpuMaterial->getBindGroup()},
				{BindGroupType::Light, m_lightBindGroup},
				{BindGroupType::Shadow, m_shadowBindGroup},
				{BindGroupType::Environment, m_environmentBindGroup},
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

		++drawn;
	}

	m_renderPassContext->end(renderPass);
	m_context->submitCommandEncoder(encoder, "ForwardTransparencyPass.Commands");

	spdlog::debug("ForwardTransparencyPass: drew {} transparent items ({} skipped)", drawn, skipped);
}

} // namespace engine::rendering
