#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"

namespace engine::rendering
{

// Forward declarations
struct FrameCache;

namespace webgpu
{
class WebGPUBindGroup;
class WebGPUShaderInfo;
} // namespace webgpu

/**
 * @brief Centralized bind group binding for render passes.
 *
 * Handles binding of engine-defined (Frame, Light, Object, Material, Shadow) and
 * user-defined (Custom) bind groups. Tracks bound groups to skip redundant binds.
 *
 * Usage:
 * @code
 *   BindGroupBinder binder(&frameCache);
 *   binder.bind(renderPass, shaderInfo, cameraId, objectId);
 *   binder.reset();
 * @endcode
 */
class BindGroupBinder
{
  public:
	explicit BindGroupBinder(FrameCache *frameCache) : m_frameCache(frameCache) {}

	/**
	 * @brief Binds all bind groups defined in the shader.
	 *
	 * Resolves bind groups from FrameCache or bindGroups map. Only binds when bind group changes.
	 *
	 * @param renderPass WebGPU render pass encoder
	 * @param shaderInfo Shader with bind group layouts
	 * @param cameraId Camera ID for frame bind group lookup
	 * @param bindGroups Map of bind group type to bind group (optional groups can be omitted)
	 * @param objectId Object ID for per-object bind group lookup (optional)
	 */
	bool bind(
		wgpu::RenderPassEncoder &renderPass,
		const std::shared_ptr<webgpu::WebGPUShaderInfo> &shaderInfo,
		uint64_t cameraId,
		const std::unordered_map<BindGroupType, std::shared_ptr<webgpu::WebGPUBindGroup>> &bindGroups = {},
		std::optional<uint64_t> objectId = std::nullopt
	);

	/**
	 * @brief Binds a specific bind group by name. Only binds if changed.
	 */
	bool bindGroupByName(
		wgpu::RenderPassEncoder &renderPass,
		const std::shared_ptr<webgpu::WebGPUShaderInfo> &shaderInfo,
		const std::string &bindGroupName,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
	);

	/**
	 * @brief Binds a bind group using its layout name. Only binds if changed.
	 */
	bool bindGroup(
		wgpu::RenderPassEncoder &renderPass,
		const std::shared_ptr<webgpu::WebGPUShaderInfo> &shaderInfo,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
	);

	/**
	 * @brief Clears bound group tracking. Call when pipeline changes.
	 */
	void reset() { m_boundBindGroups.clear(); }

  private:
	std::optional<uint64_t> getBindGroupIndex(
		const std::shared_ptr<webgpu::WebGPUShaderInfo> &shaderInfo,
		const std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> &layoutInfo
	) const;

	// Binds bind group only if it differs from currently bound group
	bool bindGroupAtIndex(
		wgpu::RenderPassEncoder &renderPass,
		uint32_t groupIndex,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
	);

	FrameCache *m_frameCache = nullptr;

	// Track bound bind groups to avoid redundant binds
	// Key: group index -> Value: bind group pointer
	std::unordered_map<uint32_t, const webgpu::WebGPUBindGroup *> m_boundBindGroups;
};

} // namespace engine::rendering
