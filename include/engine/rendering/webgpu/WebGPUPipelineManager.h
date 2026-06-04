#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Mesh.h"
#include "engine/rendering/cache/ResourceSlot.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include "engine/resources/ResourceManagerBase.h"

namespace engine::rendering::webgpu
{

class WebGPUContext;
class WebGPUPipelineFactory;

/**
 * @brief Configuration for creating a WebGPU pipeline.
 * Contains all parameters needed to define a pipeline.
 * Uses shader NAME as key, not shader pointer (shaders are immutable).
 */
struct PipelineKey
{
	std::string shaderName;						// shader identifier (immutable after creation)
	wgpu::TextureFormat colorFormat;			// from RenderTarget
	wgpu::TextureFormat depthFormat;			// from RenderTarget
	engine::rendering::Topology::Type topology; // from Mesh
	wgpu::CullMode cullMode;					// from Material / Default
	bool blendEnabled;							// from Material / Default
	uint32_t sampleCount;						// MSAA, from RenderTarget / global

	bool operator==(const PipelineKey &other) const
	{
		return shaderName == other.shaderName
			   && colorFormat == other.colorFormat
			   && depthFormat == other.depthFormat
			   && topology == other.topology
			   && cullMode == other.cullMode
			   && blendEnabled == other.blendEnabled
			   && sampleCount == other.sampleCount;
	}
};

/**
 * @brief Hash function for PipelineKey to be used in unordered_map.
 */
struct PipelineKeyHasher
{
	std::size_t operator()(const PipelineKey &key) const
	{
		std::size_t h = std::hash<std::string>{}(key.shaderName);
		h ^= std::hash<int>{}(static_cast<int>(key.colorFormat)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>{}(static_cast<int>(key.depthFormat)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>{}(static_cast<int>(key.topology)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>{}(static_cast<int>(key.cullMode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<bool>{}(key.blendEnabled) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint32_t>{}(key.sampleCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};

/**
 * @brief Manages render pipelines with hot-reloading support.
 *
 * SINGLE ENTRY POINT for all pipeline creation and management.
 *
 * **Handle pattern.** getOrCreatePipeline returns an opaque
 * `PipelineHandle` (Handle<WebGPUPipeline>) instead of a raw shared_ptr.
 * Internally the manager keeps a ResourceSlot per cached pipeline that
 * holds the strong reference. On hot reload, processPendingReloads()
 * builds the new pipeline and `slot->replace()`s the resource — every
 * outstanding handle (every render pass that stashed one) automatically
 * sees the new pipeline on its next `handle.lock()`. No call-site
 * refetch needed.
 *
 * **Reload semantics.**
 * - reloadAllPipelines() marks every cached key for rebuild.
 * - processPendingReloads() rebuilds + swaps inside each slot. Old
 *   pipelines stay alive as long as someone holds a `lock()` snapshot,
 *   which keeps in-flight GPU work safe.
 * - Per-pipeline reload was removed — the only public reload path is
 *   reloadAllPipelines(), which is what the only caller (ImGui debug
 *   panel) actually wants.
 */
class WebGPUPipelineManager
{
  public:
	using PipelineSlot   = engine::rendering::cache::ResourceSlot<WebGPUPipeline>;
	using PipelineHandle = engine::rendering::cache::Handle<WebGPUPipeline>;

	WebGPUPipelineManager(WebGPUContext &context);
	~WebGPUPipelineManager();

	/**
	 * @brief Get or create a pipeline for a mesh, material, and render target.
	 *
	 * ONLY public method for obtaining pipelines.
	 * The key is generated internally using shader info, features, vertex
	 * layout, and render target formats. Returns an empty handle on failure.
	 */
	PipelineHandle getOrCreatePipeline(
		const std::shared_ptr<engine::rendering::Mesh> &mesh,
		const std::shared_ptr<engine::rendering::Material> &material,
		const std::shared_ptr<engine::rendering::webgpu::WebGPURenderPassContext> &renderPass
	);

	/**
	 * @brief Get or create a pipeline with explicit parameters (no mesh/material required).
	 *
	 * Use this when you have all pipeline parameters but no mesh or material
	 * object — shadow / composition / postprocess passes typically take this
	 * path. Returns an empty handle on failure.
	 */
	PipelineHandle getOrCreatePipeline(
		const std::shared_ptr<WebGPUShaderInfo> &shaderInfo,
		wgpu::TextureFormat colorFormat,
		wgpu::TextureFormat depthFormat,
		engine::rendering::Topology::Type topology,
		wgpu::CullMode cullMode,
		bool blendEnabled,
		uint32_t sampleCount
	);

	/**
	 * @brief Mark all pipelines for reload after current frame finishes.
	 * @return Number of pipelines marked for reload.
	 */
	size_t reloadAllPipelines();

	/**
	 * @brief Process pending pipeline reloads (call after frame finishes and
	 *        is presented). Each reloaded pipeline is swapped INSIDE its
	 *        existing slot, so outstanding handles update automatically.
	 * @return Number of successfully reloaded pipelines.
	 */
	size_t processPendingReloads();

	/**
	 * @brief Clears all cached pipelines.
	 *
	 * Existing handles continue to return their previously-pinned snapshots
	 * via lock(), but new lock() calls will return nullptr until a future
	 * getOrCreatePipeline rebuilds a slot for the same key. Use on resize /
	 * scene change / device loss.
	 */
	void cleanup();

	/// Total cached entries. Used by CacheRegistry/debug overlays. Includes
	/// evicted-but-not-yet-cleared slots (resource is gone, slot survives so
	/// outstanding handles can auto-rebuild).
	[[nodiscard]] std::size_t cacheSize() const { return m_pipelines.size(); }

	/// Slots whose resource pointer is currently populated. Walks the map
	/// under the cache mutex — debug overlays only.
	[[nodiscard]] std::size_t aliveCount() const;

	/// Configure the age-based eviction window. After @p frames of no
	/// `Handle::lock()` access, a slot's pipeline is released; the slot
	/// itself stays alive so outstanding handles can auto-rebuild on next
	/// access. Default 0 = never evict by age (legacy behaviour). Common
	/// values: 60 for "drop after 1 second @ 60fps", 0 for "keep forever".
	void setMaxIdleFrames(uint32_t frames) { m_maxIdleFrames = frames; }
	[[nodiscard]] uint32_t maxIdleFrames() const { return m_maxIdleFrames; }

	/// Increment the internal frame counter. Hooked into CacheRegistry's
	/// notifyFrameAll() so the renderer pumps every cache once per frame.
	void notifyFrame() { m_frameCounter.fetch_add(1, std::memory_order_relaxed); }

	/// Walk slots, evict any whose lastAccessFrame is more than
	/// maxIdleFrames behind the current frame. Returns count evicted.
	/// Hooked into CacheRegistry's cleanAll(). No-op if maxIdleFrames == 0.
	std::size_t evictStale();

	/// Pass-through to the underlying factory's shared empty bind group used
	/// to fill unused pipeline-layout slots (engine convention reserves
	/// @group(0..3) so utility shaders that only use @group(4)+ must still
	/// bind something at the empty slots).
	wgpu::BindGroup getOrCreateEmptyBindGroup();

  private:
	WebGPUContext &m_context;
	std::unique_ptr<WebGPUPipelineFactory> m_pipelineFactory;

	// Pipeline cache: key -> slot. Slot is the strong owner of the resource;
	// callers hold Handles backed by the same slot. processPendingReloads()
	// calls slot->replace(...) which silently updates every outstanding handle.
	// Slots carry a build_fn that knows how to rebuild on age-eviction, so
	// callers' handles keep working even after evictStale() drops the
	// resource pointer.
	std::unordered_map<PipelineKey, std::shared_ptr<PipelineSlot>, PipelineKeyHasher> m_pipelines;

	// Keys marked for rebuild after current frame finishes. Key-based (not
	// slot-based) so the reload doesn't accidentally retain old resources
	// past their useful life.
	std::unordered_set<PipelineKey, PipelineKeyHasher> m_pendingReloads;

	// Frame counter for age-eviction. Slots hold a non-owning pointer to
	// this; the renderer ticks it once per frame via notifyFrame() through
	// CacheRegistry. Reaches the slot via the pointer passed at slot
	// construction; eviction compares slot.lastAccessFrame() against this.
	std::atomic<uint32_t> m_frameCounter{0};

	// Age-eviction window. 0 = never evict by age (keep forever until
	// reload or explicit cleanup). Use setMaxIdleFrames() to configure.
	uint32_t m_maxIdleFrames = 0;

	/**
	 * @brief Internal: build a pipeline object directly (no slot/cache).
	 *
	 * Called by getOrCreatePipeline (initial build) and processPendingReloads
	 * (rebuild for hot reload). Caller wraps the result in a Slot or replaces
	 * an existing Slot's resource.
	 */
	bool createPipelineInternal(
		const PipelineKey &key,
		const std::shared_ptr<WebGPUShaderInfo> &shaderInfo,
		std::shared_ptr<WebGPUPipeline> &outPipeline
	);
};

} // namespace engine::rendering::webgpu