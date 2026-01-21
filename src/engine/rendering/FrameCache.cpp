#include "engine/rendering/FrameCache.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace engine::rendering
{

bool FrameCache::prepareGPUResources(
	std::shared_ptr<webgpu::WebGPUContext> context,
	const RenderCollector &collector,
	const std::vector<size_t> &indicesToPrepare
)
{
	// Make sure the FrameCache GPU item cache matches CPU items
	if (gpuRenderItems.size() != collector.getRenderItems().size())
		gpuRenderItems.resize(collector.getRenderItems().size());

	auto objectBindGroupLayout = context->bindGroupFactory().getGlobalBindGroupLayout("objectUniforms");
	if (!objectBindGroupLayout)
	{
		spdlog::error("Failed to get objectUniforms bind group layout");
		return false;
	}

	// Static cache for object bind groups (shared across all frames)
	static std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> s_objectBindGroupCache;

	const auto &cpuItems = collector.getRenderItems();
	for (size_t idx : indicesToPrepare)
	{
		// Skip if already prepared
		if (gpuRenderItems[idx].has_value())
			continue;

		const auto &cpuItem = cpuItems[idx];

		// Create GPU model (factory caches internally)
		auto gpuModel = context->modelFactory().createFromHandle(cpuItem.modelHandle);
		if (!gpuModel)
		{
			spdlog::warn("Failed to create GPU model for handle {}", cpuItem.modelHandle.id());
			continue;
		}

		gpuModel->syncIfNeeded();

		// Get GPU mesh
		auto gpuMesh = gpuModel->getMesh().get();
		if (!gpuMesh)
		{
			spdlog::warn("Failed to get GPU mesh from model {}", cpuItem.modelHandle.id());
			continue;
		}

		gpuMesh->syncIfNeeded();

		// Get GPU material
		auto materialHandle = cpuItem.submesh.material;
		auto gpuMaterial = context->materialFactory().createFromHandle(materialHandle);
		if (!gpuMaterial)
		{
			spdlog::warn("Failed to create GPU material for submesh");
			continue;
		}

		gpuMaterial->syncIfNeeded();

		// Get or create object bind group
		std::shared_ptr<webgpu::WebGPUBindGroup> objectBindGroup;
		auto it = s_objectBindGroupCache.find(cpuItem.objectID);
		if (it != s_objectBindGroupCache.end())
		{
			objectBindGroup = it->second;
		}
		else
		{
			objectBindGroup = context->bindGroupFactory().createBindGroup(objectBindGroupLayout);
			if (cpuItem.objectID != 0)
				s_objectBindGroupCache[cpuItem.objectID] = objectBindGroup;
		}

		auto objectUniforms = ObjectUniforms{cpuItem.worldTransform, glm::inverseTranspose(cpuItem.worldTransform)};
		objectBindGroup->updateBuffer(
			0,
			&objectUniforms,
			sizeof(ObjectUniforms),
			0,
			context->getQueue()
		);

		// Fill GPU render item
		RenderItemGPU gpuItem;
		gpuItem.gpuModel = gpuModel;
		gpuItem.gpuMesh = gpuMesh;
		gpuItem.gpuMaterial = gpuMaterial;
		gpuItem.objectBindGroup = objectBindGroup;
		gpuItem.submesh = cpuItem.submesh;
		gpuItem.worldTransform = cpuItem.worldTransform;
		gpuItem.renderLayer = cpuItem.renderLayer;
		gpuItem.objectID = cpuItem.objectID;

		gpuRenderItems[idx] = gpuItem;
	}

	spdlog::debug(
		"Prepared GPU resources: {}/{} items",
		std::count_if(gpuRenderItems.begin(), gpuRenderItems.end(), [](auto &i)
					  { return i.has_value(); }),
		collector.getRenderItems().size()
	);

	return true;
}

} // namespace engine::rendering
