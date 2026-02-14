#include "engine/rendering/BindGroupBinder.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{

bool BindGroupBinder::bind(
	wgpu::RenderPassEncoder &renderPass,
	const std::shared_ptr<webgpu::WebGPUShaderInfo> &shaderInfo,
	uint64_t cameraId,
	const std::unordered_map<webgpu::BindGroupType, std::shared_ptr<webgpu::WebGPUBindGroup>> &bindGroups,
	std::optional<uint64_t> objectId
)
{
	if (!shaderInfo)
	{
		spdlog::error("BindGroupBinder::bind() - Invalid shader info");
		return false;
	}

	bool allBound = true;

	// Iterate through all bind group layouts defined in the shader
	auto layouts = shaderInfo->getBindGroupLayoutVector();
	for (const auto &layoutInfo : layouts)
	{
		if (!layoutInfo)
			continue;

		// Get the group index for this layout
		auto indexOpt = shaderInfo->getBindGroupIndex(layoutInfo->getName());
		if (!indexOpt.has_value())
		{
			spdlog::warn("BindGroupBinder: Could not find group index for bind group '{}'", layoutInfo->getName());
			continue;
		}
		uint32_t groupIndex = static_cast<uint32_t>(indexOpt.value());

		std::shared_ptr<webgpu::WebGPUBindGroup> bindGroup = nullptr;

		switch (layoutInfo->getType())
		{
		case webgpu::BindGroupType::Frame:
			{
				auto it = m_frameCache->frameBindGroupCache.find(cameraId);
				if (it != m_frameCache->frameBindGroupCache.end())
				{
					bindGroup = it->second;
				}
				else if (!m_frameCache->frameBindGroupCache.empty())
				{
					// Fallback to first available if camera ID not found
					spdlog::warn("BindGroupBinder: Frame bind group not found for camera ID {}, using first available", cameraId);
					bindGroup = m_frameCache->frameBindGroupCache.begin()->second;
				}
			}
			break;

		case webgpu::BindGroupType::Light:
			{
				auto it = bindGroups.find(webgpu::BindGroupType::Light);
				if (it != bindGroups.end())
					bindGroup = it->second;
			}
			break;

		case webgpu::BindGroupType::Object:
			{
				auto it = bindGroups.find(webgpu::BindGroupType::Object);
				if (it != bindGroups.end())
				{
					bindGroup = it->second;
				}
				else if (objectId.has_value())
				{
					auto cacheIt = m_frameCache->objectBindGroupCache.find(objectId.value());
					if (cacheIt != m_frameCache->objectBindGroupCache.end())
					{
						bindGroup = cacheIt->second;
					}
				}
			}
			break;

		case webgpu::BindGroupType::Material:
			{
				auto it = bindGroups.find(webgpu::BindGroupType::Material);
				if (it != bindGroups.end())
					bindGroup = it->second;
			}
			break;

		case webgpu::BindGroupType::Shadow:
			{
				auto it = bindGroups.find(webgpu::BindGroupType::Shadow);
				if (it != bindGroups.end())
					bindGroup = it->second;
			}
			break;

		case webgpu::BindGroupType::ShadowPass2D:
			{
				auto it = bindGroups.find(webgpu::BindGroupType::ShadowPass2D);
				if (it != bindGroups.end())
					bindGroup = it->second;
			}
			break;

		case webgpu::BindGroupType::ShadowPassCube:
			{
				auto it = bindGroups.find(webgpu::BindGroupType::ShadowPassCube);
				if (it != bindGroups.end())
					bindGroup = it->second;
			}
			break;

		case webgpu::BindGroupType::Custom:
		{
			auto reuse = layoutInfo->getReuse();
			std::optional<uint64_t> instanceId;

			if (reuse == webgpu::BindGroupReuse::PerObject || reuse == webgpu::BindGroupReuse::PerMaterial)
			{
				instanceId = objectId;
			}
			// else: Global or PerFrame - no instance ID needed

			std::string cacheKey = FrameCache::createCustomBindGroupCacheKey(
				shaderInfo->getName(),
				layoutInfo->getName(),
				instanceId
			);

			auto it = m_frameCache->customBindGroupCache.find(cacheKey);
			if (it != m_frameCache->customBindGroupCache.end())
			{
				bindGroup = it->second;
			}
			else
			{
				spdlog::debug(
					"BindGroupBinder::bindShaderGroups() - Custom bind group '{}' not found for shader '{}' (may not be needed)",
					layoutInfo->getName(),
					shaderInfo->getName()
				);
				// Don't mark as error - custom groups are optional
				continue;
			}
			break;
		}

		case webgpu::BindGroupType::Debug:
			{
				auto it = bindGroups.find(webgpu::BindGroupType::Debug);
				if (it != bindGroups.end())
					bindGroup = it->second;
			}
			break;

		case webgpu::BindGroupType::Mipmap:
			// Mipmap bind group is used in mipmap generation pass (not main rendering)
			// Should not be bound here
			spdlog::warn("Attempted to bind Mipmap bind group in main rendering - this should not happen");
			break;

		default:
			spdlog::warn(
				"BindGroupBinder::bind() - Unhandled bind group type {} for group {}",
				static_cast<int>(layoutInfo->getType()),
				groupIndex
			);
			allBound = false;
			continue;
		}

		// Bind the group if we found one
		if (bindGroup)
		{
			if (!bindGroupAtIndex(renderPass, groupIndex, bindGroup))
			{
				allBound = false;
			}
		}
		else
		{
			// Log missing bind groups but don't fail - shader will error if it actually needs it
			spdlog::trace(
				"BindGroupBinder::bind() - No bind group found for type {} at group {} (shader: {})",
				static_cast<int>(layoutInfo->getType()),
				groupIndex,
				shaderInfo->getName()
			);
		}
	}

	return allBound;
}

bool BindGroupBinder::bindGroupByName(
	wgpu::RenderPassEncoder &renderPass,
	const std::shared_ptr<webgpu::WebGPUShaderInfo> &shaderInfo,
	const std::string &bindGroupName,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
)
{
	if (!shaderInfo || !bindGroup)
		return false;

	// Get bind group index by name
	auto indexOpt = shaderInfo->getBindGroupIndex(bindGroupName);
	if (!indexOpt.has_value())
	{
		spdlog::warn(
			"BindGroupBinder::bindGroupByName() - Bind group '{}' not found in shader '{}'",
			bindGroupName,
			shaderInfo->getName()
		);
		return false;
	}

	uint32_t groupIndex = static_cast<uint32_t>(indexOpt.value());
	return bindGroupAtIndex(renderPass, groupIndex, bindGroup);
}

bool BindGroupBinder::bindGroupAtIndex(
	wgpu::RenderPassEncoder &renderPass,
	uint32_t groupIndex,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
)
{
	if (!bindGroup)
		return false;

	// Check if already bound
	auto it = m_boundBindGroups.find(groupIndex);
	if (it != m_boundBindGroups.end() && it->second == bindGroup.get())
	{
		// Already bound, skip
		return true;
	}

	// Bind the group
	renderPass.setBindGroup(groupIndex, bindGroup->getBindGroup(), 0, nullptr);
	m_boundBindGroups[groupIndex] = bindGroup.get();

	spdlog::trace("Bound bind group at index {}", groupIndex);
	return true;
}

bool BindGroupBinder::bindGroup(
	wgpu::RenderPassEncoder &renderPass,
	const std::shared_ptr<webgpu::WebGPUShaderInfo> &shaderInfo,
	const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
)
{
	if (!bindGroup || !bindGroup->getLayoutInfo())
	{
		spdlog::warn("BindGroupBinder::bindGroup() - Invalid bind group or layout info");
		return false;
	}

	// Extract name from bind group's layout info
	const std::string &bindGroupName = bindGroup->getLayoutInfo()->getName();
	return bindGroupByName(renderPass, shaderInfo, bindGroupName, bindGroup);
}

std::optional<uint64_t> BindGroupBinder::getBindGroupIndex(
	const std::shared_ptr<webgpu::WebGPUShaderInfo> &shaderInfo,
	const std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> &layoutInfo
) const
{
	if (!shaderInfo || !layoutInfo)
		return std::nullopt;

	// Use shader's built-in getBindGroupIndex method
	return shaderInfo->getBindGroupIndex(layoutInfo->getName());
}

} // namespace engine::rendering
