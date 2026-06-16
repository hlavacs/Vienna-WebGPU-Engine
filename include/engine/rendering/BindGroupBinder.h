#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/BindGroupEnums.h"

namespace engine::rendering
{

// Forward declarations
struct FrameCache;

namespace webgpu
{
class WebGPUBindGroup;
class WebGPUBindGroupLayoutInfo;
class WebGPUPipeline;
class WebGPUContext;
} // namespace webgpu

/**
 * @brief Centralized bind group binding for render passes.
 *
 * Automatically tracks state and only rebinds when necessary:
 * - Render pass changed → rebind all
 * - Pipeline changed → rebind all
 * - Camera changed → rebind Frame + PerFrame Custom
 * - Object changed → rebind Object + PerObject Custom
 * - Material changed → rebind Material + PerMaterial Custom
 * - Bind group pointer changed → rebind that group
 *
 * No manual reset() needed - fully automatic.
 *
 * Usage:
 * @code
 *   BindGroupBinder binder(&frameCache);
 *   
 *   // Simple: just camera and object
 *   binder.bind(renderPass, pipeline, cameraId, {}, objectId);
 *   
 *   // With explicit material bind group
 *   binder.bind(renderPass, pipeline, cameraId, 
 *               {{BindGroupType::Material, materialBG}}, 
 *               objectId, materialId);
 * @endcode
 */
class BindGroupBinder
{
  public:
	explicit BindGroupBinder(FrameCache *frameCache) : m_frameCache(frameCache) {}

	/// Optional context binding so the binder can fill unused @group slots
	/// with the engine-wide empty bind group (required by wgpu when the
	/// pipeline layout has empty placeholders at lower indices than the
	/// shader actually uses).
	void setContext(webgpu::WebGPUContext *context) { m_context = context; }

	/**
	 * @brief Binds all bind groups defined in the shader's layout.
	 *
	 * Bind groups are sourced from:
	 * 1. The bindGroups parameter (explicit overrides)
	 * 2. FrameCache (Frame, Object caches based on IDs)
	 * 3. Custom bind group cache (for user-defined bind groups)
	 *
	 * @param renderPass WebGPU render pass encoder
	 * @param pipeline Pipeline containing shader info and layout
	 * @param cameraId Camera ID for Frame bind group lookup
	 * @param bindGroups Explicit bind groups to use (overrides cache lookup)
	 * @param objectId Object ID for Object bind group lookup (optional)
	 * @param materialId Material ID for Material bind group lookup (optional)
	 * @return true if all required bind groups were bound successfully
	 */
	bool bind(
		wgpu::RenderPassEncoder &renderPass,
		const std::shared_ptr<webgpu::WebGPUPipeline> &pipeline,
		uint64_t cameraId,
		const std::unordered_map<BindGroupType, std::shared_ptr<webgpu::WebGPUBindGroup>> &bindGroups = {},
		std::optional<uint64_t> objectId = std::nullopt,
		std::optional<uint64_t> materialId = std::nullopt
	);

  private:
	/**
	 * @brief Finds the appropriate bind group based on type and reuse policy.
	 * 
	 * Lookup order:
	 * 1. Custom bind groups → customBindGroupCache
	 * 2. Explicit bindGroups parameter
	 * 3. Type-specific caches (frameBindGroupCache, objectBindGroupCache)
	 */
	std::shared_ptr<webgpu::WebGPUBindGroup> findBindGroup(
		const std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> &layoutInfo,
		const std::string &shaderName,
		const std::unordered_map<BindGroupType, std::shared_ptr<webgpu::WebGPUBindGroup>> &bindGroups,
		uint64_t cameraId,
		std::optional<uint64_t> objectId,
		std::optional<uint64_t> materialId
	);

	/**
	 * @brief Binds a bind group if it differs from the currently bound group.
	 * @return true if bound or already bound with same group
	 */
	bool bindGroupAtIndex(
		wgpu::RenderPassEncoder &renderPass,
		uint32_t groupIndex,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup
	);

	// Dependencies
	FrameCache *m_frameCache = nullptr;
	webgpu::WebGPUContext *m_context = nullptr;

	// State tracking for automatic rebinding detection
	WGPURenderPassEncoder m_lastRenderPassHandle = nullptr;
	uint64_t m_lastCameraId = 0;
	std::optional<uint64_t> m_lastObjectId;
	std::optional<uint64_t> m_lastMaterialId;

	/// wgpu's spec maximum bind groups per pipeline. The engine convention
	/// uses @group(0..3) for the canonical Frame/Scene/Material/Object set
	/// plus @group(4..7) for pass-specific custom groups. Anything outside
	/// 0..7 is invalid; the binder asserts on out-of-range slot indices.
	static constexpr uint32_t kMaxBindGroups = 8;

	/// Currently bound bind groups, keyed by @group index. Array (not
	/// hashmap) because this is touched on every layout iteration of every
	/// draw call — that's potentially N_layouts × N_draws hashmap lookups
	/// per frame, vs. the same number of direct array indexes. nullptr =
	/// nothing bound, the sentinel `reinterpret_cast<...>(uintptr_t(1))` =
	/// the shared empty bind group (see bind() — that lets us still detect
	/// a real bind-group later replacing it).
	std::array<const webgpu::WebGPUBindGroup *, kMaxBindGroups> m_boundBindGroups{};
};

} // namespace engine::rendering