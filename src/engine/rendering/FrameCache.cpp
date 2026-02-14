#include "engine/rendering/FrameCache.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupDataProvider.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/ShaderRegistry.h"
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

bool FrameCache::processBindGroupProviders(
	std::shared_ptr<webgpu::WebGPUContext> context,
	const std::vector<BindGroupDataProvider> &providers
)
{
	bool allSuccessful = true;

	for (const auto &provider : providers)
	{
		// Create cache key using static helper method
		std::string cacheKey = createCustomBindGroupCacheKey(
			provider.shaderName,
			provider.bindGroupName,
			provider.instanceId
		);

		// Check if bind group already exists in cache
		auto it = customBindGroupCache.find(cacheKey);
		if (it != customBindGroupCache.end())
		{
			// Bind group found in cache - just update the data
			auto &bindGroup = it->second;
			bindGroup->updateBuffer(
				0,							// binding index
				provider.data.data(),		// data pointer
				provider.dataSize,			// data size
				0,							// offset
				context->getQueue()
			);
			continue; // Skip to next provider
		}

		// Bind group not cached - need to create it
		// Get shader info
		auto shaderInfo = context->shaderRegistry().getShader(provider.shaderName);
		if (!shaderInfo)
		{
			spdlog::error(
				"FrameCache::processBindGroupProviders() - Shader '{}' not found",
				provider.shaderName
			);
			allSuccessful = false;
			continue;
		}

		// Get bind group layout by name
		auto layoutInfo = shaderInfo->getBindGroupLayout(provider.bindGroupName);
		if (!layoutInfo)
		{
			spdlog::error(
				"FrameCache::processBindGroupProviders() - Bind group '{}' not found in shader '{}'",
				provider.bindGroupName,
				provider.shaderName
			);
			allSuccessful = false;
			continue;
		}

		// Validate BindGroupReuse matches shader definition (only on first creation)
		auto shaderReuse = layoutInfo->getReuse();
		if (provider.reuse != shaderReuse)
		{
			spdlog::warn(
				"FrameCache::processBindGroupProviders() - BindGroupReuse mismatch for '{}'.'{}': "
				"Provider specifies {:d}, but shader defines {:d}. Using shader's definition.",
				provider.shaderName,
				provider.bindGroupName,
				static_cast<int>(provider.reuse),
				static_cast<int>(shaderReuse)
			);
		}

		// Validate instanceId matches reuse policy (only on first creation)
		bool needsInstance = (shaderReuse == webgpu::BindGroupReuse::PerObject || 
		                      shaderReuse == webgpu::BindGroupReuse::PerMaterial);
		bool hasInstance = provider.instanceId.has_value();
		
		if (needsInstance && !hasInstance)
		{
			spdlog::error(
				"FrameCache::processBindGroupProviders() - Bind group '{}'.'{}' requires instanceId "
				"(reuse={:d}), but none provided. Each object/material must have unique instanceId.",
				provider.shaderName,
				provider.bindGroupName,
				static_cast<int>(shaderReuse)
			);
			allSuccessful = false;
			continue;
		}
		
		if (!needsInstance && hasInstance)
		{
			spdlog::warn(
				"FrameCache::processBindGroupProviders() - Bind group '{}'.'{}' is shared (reuse={:d}), "
				"but instanceId provided. The instanceId will be ignored and all objects will share this bind group.",
				provider.shaderName,
				provider.bindGroupName,
				static_cast<int>(shaderReuse)
			);
		}

		// Create bind group
		auto bindGroup = context->bindGroupFactory().createBindGroup(layoutInfo);
		if (!bindGroup)
		{
			spdlog::error(
				"FrameCache::processBindGroupProviders() - Failed to create bind group '{}' for shader '{}'",
				provider.bindGroupName,
				provider.shaderName
			);
			allSuccessful = false;
			continue;
		}

		// Update buffer with initial data
		bindGroup->updateBuffer(
			0,							// binding index
			provider.data.data(),		// data pointer
			provider.dataSize,			// data size
			0,							// offset
			context->getQueue()
		);

		// Cache bind group for future use
		customBindGroupCache[cacheKey] = bindGroup;
		spdlog::debug(
			"Created custom bind group '{}' for shader '{}' (cached as '{}')",
			provider.bindGroupName,
			provider.shaderName,
			cacheKey
		);
	}

	return allSuccessful;
}

bool FrameCache::prepareGPUResources(
	std::shared_ptr<webgpu::WebGPUContext> context,
	const RenderCollector &collector,
	const std::vector<size_t> &indicesToPrepare
)
{
	// Make sure the FrameCache GPU item cache matches CPU items
	if (gpuRenderItems.size() != collector.getRenderItems().size())
		gpuRenderItems.resize(collector.getRenderItems().size());

	auto objectBindGroupLayout = context->bindGroupFactory().getGlobalBindGroupLayout(bindgroup::defaults::OBJECT);
	if (!objectBindGroupLayout)
	{
		spdlog::error("Failed to get objectUniforms bind group layout");
		return false;
	}

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
		auto it = objectBindGroupCache.find(cpuItem.objectID);
		if (it != objectBindGroupCache.end())
		{
			objectBindGroup = it->second;
		}
		else
		{
			objectBindGroup = context->bindGroupFactory().createBindGroup(objectBindGroupLayout);
			if (cpuItem.objectID != 0)
				objectBindGroupCache[cpuItem.objectID] = objectBindGroup;
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
