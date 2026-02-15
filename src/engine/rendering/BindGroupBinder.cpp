#include "engine/rendering/BindGroupBinder.h"
#include <spdlog/spdlog.h>
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{

bool BindGroupBinder::bind(
	wgpu::RenderPassEncoder &renderPass,
	const std::shared_ptr<webgpu::WebGPUPipeline> &pipeline,
	uint64_t cameraId,
	const std::unordered_map<BindGroupType, std::shared_ptr<webgpu::WebGPUBindGroup>> &bindGroups,
	std::optional<uint64_t> objectId,
	std::optional<uint64_t> materialId
)
{
	if (!pipeline || !pipeline->getShaderInfo())
	{
		spdlog::error("BindGroupBinder: Invalid pipeline or shader info");
		return false;
	}

	auto shaderInfo = pipeline->getShaderInfo();

	// Detect render pass change
	WGPURenderPassEncoder currentHandle = static_cast<WGPURenderPassEncoder>(renderPass);
	if (m_lastRenderPassHandle != currentHandle)
	{
		m_lastRenderPassHandle = currentHandle;
		m_boundBindGroups.clear();
		spdlog::trace("BindGroupBinder: New render pass");
	}

	// Detect state changes
	bool cameraChanged = (m_lastCameraId != cameraId);
	bool objectChanged = (m_lastObjectId != objectId);
	bool materialChanged = (m_lastMaterialId != materialId);

	if (cameraChanged) m_lastCameraId = cameraId;
	if (objectChanged) m_lastObjectId = objectId;
	if (materialChanged) m_lastMaterialId = materialId;

	// Bind all groups declared by shader
	bool allBound = true;
	for (const auto &layoutInfo : shaderInfo->getBindGroupLayoutVector())
	{
		if (!layoutInfo) continue;

		// Get group index
		auto indexOpt = shaderInfo->getBindGroupIndex(layoutInfo->getName());
		if (!indexOpt.has_value()) continue;
		uint32_t groupIndex = static_cast<uint32_t>(indexOpt.value());

		// Check if we need to rebind based on reuse policy
		bool needsRebind = false;
		switch (layoutInfo->getReuse())
		{
			case BindGroupReuse::Global: needsRebind = false; break;
			case BindGroupReuse::PerFrame: needsRebind = cameraChanged; break;
			case BindGroupReuse::PerObject: needsRebind = objectChanged; break;
			case BindGroupReuse::PerMaterial: needsRebind = materialChanged; break;
		}

		// Find the bind group
		std::shared_ptr<webgpu::WebGPUBindGroup> bindGroup = findBindGroup(
			layoutInfo, 
			shaderInfo->getName(),
			bindGroups, 
			cameraId, 
			objectId, 
			materialId
		);

		if (!bindGroup)
		{
			spdlog::trace("BindGroupBinder: No bind group for '{}' at group {}", layoutInfo->getName(), groupIndex);
			continue;
		}

		// Bind if needed
		if (needsRebind || m_boundBindGroups[groupIndex] != bindGroup.get())
		{
			if (!bindGroupAtIndex(renderPass, groupIndex, bindGroup))
				allBound = false;
		}
	}

	return allBound;
}

std::shared_ptr<webgpu::WebGPUBindGroup> BindGroupBinder::findBindGroup(
	const std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> &layoutInfo,
	const std::string &shaderName,
	const std::unordered_map<BindGroupType, std::shared_ptr<webgpu::WebGPUBindGroup>> &bindGroups,
	uint64_t cameraId,
	std::optional<uint64_t> objectId,
	std::optional<uint64_t> materialId
)
{
	auto type = layoutInfo->getType();
	auto reuse = layoutInfo->getReuse();

	// Custom bind groups - look in cache
	if (type == BindGroupType::Custom)
	{
		std::optional<uint64_t> instanceId;
		if (reuse == BindGroupReuse::PerObject) instanceId = objectId;
		if (reuse == BindGroupReuse::PerMaterial) instanceId = materialId;

		std::string cacheKey = FrameCache::createCustomBindGroupCacheKey(
			shaderName, 
			layoutInfo->getName(), 
			instanceId
		);

		auto it = m_frameCache->customBindGroupCache.find(cacheKey);
		return (it != m_frameCache->customBindGroupCache.end()) ? it->second : nullptr;
	}

	// Built-in bind groups - check parameter first
	auto it = bindGroups.find(type);
	if (it != bindGroups.end())
		return it->second;

	// Fallback to caches
	if (type == BindGroupType::Frame)
	{
		auto cacheIt = m_frameCache->frameBindGroupCache.find(cameraId);
		return (cacheIt != m_frameCache->frameBindGroupCache.end()) ? cacheIt->second : nullptr;
	}

	if (type == BindGroupType::Object && objectId.has_value())
	{
		auto cacheIt = m_frameCache->objectBindGroupCache.find(objectId.value());
		return (cacheIt != m_frameCache->objectBindGroupCache.end()) ? cacheIt->second : nullptr;
	}

	return nullptr;
}

bool BindGroupBinder::bindGroupAtIndex(
	wgpu::RenderPassEncoder &renderPass,
	uint32_t groupIndex,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
)
{
	if (!bindGroup) return false;

	// Check if already bound
	auto it = m_boundBindGroups.find(groupIndex);
	if (it != m_boundBindGroups.end() && it->second == bindGroup.get())
	{
		spdlog::trace("BindGroupBinder: Group {} already bound", groupIndex);
		return true;
	}

	// Bind it
	renderPass.setBindGroup(groupIndex, bindGroup->getBindGroup(), 0, nullptr);
	m_boundBindGroups[groupIndex] = bindGroup.get();

	spdlog::trace("BindGroupBinder: Bound group {}", groupIndex);
	return true;
}

} // namespace engine::rendering