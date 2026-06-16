#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Mesh.h"
#include "engine/rendering/cache/ResourceSlot.h"
#include "engine/rendering/cache/SlotCache.h"
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
 * Internally the manager keeps a `SlotCache<PipelineKey, WebGPUPipeline>`
 * — one slot per cached pipeline, each carrying a `build_fn` lambda that
 * knows how to rebuild from the shader registry. Every outstanding handle
 * (every render pass that stashed one) automatically picks up the new
 * pipeline on its next `handle.lock()` after a reload. No call-site
 * refetch needed.
 *
 * **Reload semantics.**
 * - `reloadAllPipelines()` reloads every shader source synchronously via
 *   `WebGPUShaderFactory::reloadShader`, then calls `m_pipelines
 *   .clearResources()` to drop every slot's pipeline pointer. The next
 *   `handle.lock()` triggers the slot's captured `build_fn`, which looks
 *   up the freshly-reloaded shader from the registry and rebuilds the
 *   pipeline transparently. Safe mid-frame: the previous pipeline stays
 *   alive as long as any pinned `lock()` snapshot holds it.
 * - Per-pipeline reload was removed — the only public reload path is
 *   `reloadAllPipelines()`, which is what the only caller (ImGui debug
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
	 * @brief Reload every shader source and soft-clear every pipeline slot.
	 *
	 * Outstanding `PipelineHandle`s keep working — their next `lock()`
	 * triggers the slot's captured `build_fn`, which fetches the
	 * freshly-reloaded shader info from the registry and recreates the
	 * pipeline transparently. Old pipelines stay alive in any pinned
	 * `lock()` snapshot, so this is safe to call mid-frame.
	 *
	 * @return Number of pipeline slots soft-cleared.
	 */
	size_t reloadAllPipelines();

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
	[[nodiscard]] std::size_t cacheSize() const { return m_pipelines.cacheSize(); }

	/// Slots whose resource pointer is currently populated. Walks the map
	/// under the cache mutex — debug overlays only.
	[[nodiscard]] std::size_t aliveCount() const { return m_pipelines.aliveCount(); }

	/// Configure the age-based eviction window. After @p frames of no
	/// `Handle::lock()` access, a slot's pipeline is released; the slot
	/// itself stays alive so outstanding handles can auto-rebuild on next
	/// access. Default 0 = never evict by age (legacy behaviour). Common
	/// values: 60 for "drop after 1 second @ 60fps", 0 for "keep forever".
	void                   setMaxIdleFrames(uint32_t frames) { m_pipelines.setMaxIdleFrames(frames); }
	[[nodiscard]] uint32_t maxIdleFrames() const { return m_pipelines.maxIdleFrames(); }

	/// Increment the internal frame counter. Hooked into CacheRegistry's
	/// notifyFrameAll() so the renderer pumps every cache once per frame.
	void notifyFrame() { m_pipelines.notifyFrame(); }

	/// Walk slots, evict any whose lastAccessFrame is more than
	/// maxIdleFrames behind the current frame. Returns count evicted.
	/// Hooked into CacheRegistry's cleanAll(). No-op if maxIdleFrames == 0.
	std::size_t evictStale() { return m_pipelines.evictStale(); }

	/// Pass-through to the underlying factory's shared empty bind group used
	/// to fill unused pipeline-layout slots (engine convention reserves
	/// @group(0..3) so utility shaders that only use @group(4)+ must still
	/// bind something at the empty slots).
	wgpu::BindGroup getOrCreateEmptyBindGroup();

	/// Direct access to the underlying pipeline factory for low-level,
	/// uncached pipeline creation the cached getOrCreatePipeline path doesn't
	/// cover — compute pipelines, one-shot bakes. Most callers want
	/// getOrCreatePipeline instead.
	WebGPUPipelineFactory &factory();

  private:
	WebGPUContext                         &m_context;
	std::unique_ptr<WebGPUPipelineFactory> m_pipelineFactory;

	// Pipeline cache: key -> slot. SlotCache is the strong owner of the
	// resource via per-key slots; callers hold Handles backed by the same
	// slot. Each slot's build_fn looks up the current shader info from the
	// registry and recreates the pipeline — used for both the initial build
	// on cache miss, transparent rebuild after age-eviction, and the
	// soft-clear path inside reloadAllPipelines().
	engine::rendering::cache::SlotCache<PipelineKey, WebGPUPipeline, PipelineKeyHasher> m_pipelines;

	/**
	 * @brief Internal: build a pipeline object directly (no slot/cache).
	 *
	 * Called by the slot's build_fn lambda — both for the initial build
	 * on cache miss and for transparent rebuild after eviction or
	 * soft-clear. The SlotCache wraps the returned pipeline in a slot.
	 */
	bool createPipelineInternal(
		const PipelineKey                       &key,
		const std::shared_ptr<WebGPUShaderInfo> &shaderInfo,
		std::shared_ptr<WebGPUPipeline>         &outPipeline
	);
};

} // namespace engine::rendering::webgpu