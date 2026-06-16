#include "engine/rendering/BindGroupBinder.h"
#include <spdlog/spdlog.h>
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
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
		m_boundBindGroups.fill(nullptr);
		spdlog::trace("BindGroupBinder: New render pass");
	}

	// Detect state changes
	bool cameraChanged = (m_lastCameraId != cameraId);
	bool objectChanged = (m_lastObjectId != objectId);
	bool materialChanged = (m_lastMaterialId != materialId);

	if (cameraChanged) m_lastCameraId = cameraId;
	if (objectChanged) m_lastObjectId = objectId;
	if (materialChanged) m_lastMaterialId = materialId;

	// Bind all groups declared by shader. Engine convention reserves
	// @group(0..3) for Frame/Scene/Material/Object even if a particular
	// shader doesn't use them all — wgpu requires SOMETHING bound at every
	// pipeline-layout slot, so unused slots get the shared empty bind group.
	const auto &layoutVector = shaderInfo->getBindGroupLayoutVector();
	if (!layoutVector.empty() && pipeline->getShaderInfo())
	{
		auto emptyBg = m_context ? m_context->pipelineManager().getOrCreateEmptyBindGroup() : nullptr;
		const uint32_t layoutCount = std::min<uint32_t>(
			static_cast<uint32_t>(layoutVector.size()), kMaxBindGroups);
		for (uint32_t slot = 0; slot < layoutCount; ++slot)
		{
			if (!layoutVector[slot] && emptyBg && m_boundBindGroups[slot] == nullptr)
			{
				renderPass.setBindGroup(slot, emptyBg, 0, nullptr);
				// Sentinel — distinct from any real bind-group pointer so the
				// rebind check above triggers if a real group later replaces it.
				m_boundBindGroups[slot] = reinterpret_cast<webgpu::WebGPUBindGroup *>(uintptr_t(1));
			}
		}
	}

	bool allBound = true;
	for (const auto &layoutInfo : layoutVector)
	{
		if (!layoutInfo) continue;

		// Get group index
		auto indexOpt = shaderInfo->getBindGroupIndex(layoutInfo->getName());
		if (!indexOpt.has_value()) continue;
		uint32_t groupIndex = static_cast<uint32_t>(indexOpt.value());

		if (groupIndex >= kMaxBindGroups)
		{
			spdlog::error("BindGroupBinder: group index {} exceeds wgpu's max of {}",
				groupIndex, kMaxBindGroups);
			allBound = false;
			continue;
		}

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
	if (groupIndex >= kMaxBindGroups)
	{
		spdlog::error("BindGroupBinder: group index {} exceeds wgpu's max of {}",
			groupIndex, kMaxBindGroups);
		return false;
	}

	// Already bound? Direct array access — no hashmap lookup per call.
	if (m_boundBindGroups[groupIndex] == bindGroup.get())
	{
		spdlog::trace("BindGroupBinder: Group {} already bound", groupIndex);
		return true;
	}

	renderPass.setBindGroup(groupIndex, bindGroup->getBindGroup(), 0, nullptr);
	m_boundBindGroups[groupIndex] = bindGroup.get();

	spdlog::trace("BindGroupBinder: Bound group {}", groupIndex);
	return true;
}

} // namespace engine::rendering