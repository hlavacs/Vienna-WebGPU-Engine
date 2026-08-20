#pragma once

#include <memory>
#include <string>

#include <webgpu/webgpu.hpp>

#include "engine/debug/Loggable.h"
#include "engine/rendering/cache/SlotCache.h"
#include "engine/rendering/webgpu/WebGPUSampler.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

/**
 * @brief Predefined sampler names for common use cases.
 */
namespace SamplerNames
{
inline constexpr const char *DEFAULT           = "default";
inline constexpr const char *MIPMAP_LINEAR     = "mipmap_linear";
inline constexpr const char *CLAMP_LINEAR      = "clamp_linear";
inline constexpr const char *CLAMP_NEAREST     = "clamp_nearest";
inline constexpr const char *REPEAT_LINEAR     = "repeat_linear";
inline constexpr const char *SHADOW_COMPARISON = "shadow_comparison";
} // namespace SamplerNames

/**
 * @brief Factory + cache for engine-wide samplers.
 *
 * Backed by `cache::SlotCache<std::string, WebGPUSampler>` — every cached
 * sampler is wrapped in a `WebGPUSampler` RAII handle that owns its own
 * wgpu refcount. Consumers receive `std::shared_ptr<WebGPUSampler>`, so a
 * sampler stays alive as long as any pass holds onto its shared_ptr. The
 * factory's `clearResources()` (called from "Clear All") only drops the
 * factory's strong ref; the underlying wgpu sampler remains valid for any
 * outstanding consumer until they drop their shared_ptr, at which point
 * `WebGPUSampler`'s destructor releases the last refcount.
 *
 * This eliminates the historical refcount-sharing crash where pass members
 * stored value-typed `wgpu::Sampler` copies and the factory's cleanup
 * destroyed the underlying object out from under them.
 */
class WebGPUSamplerFactory : public debug::Loggable
{
  public:
	using SamplerPtr = std::shared_ptr<WebGPUSampler>;

	explicit WebGPUSamplerFactory(WebGPUContext &context);

	/**
	 * @brief Get or create a named sampler.
	 *
	 * Known names (the `SamplerNames` constants) are lazy-built on first
	 * access via the slot's captured build_fn. Unknown names log a warning
	 * and fall back to the default sampler.
	 */
	SamplerPtr getSampler(const std::string &name);

	/**
	 * @brief Create a sampler with an explicit descriptor and register it
	 *        under @p name. Replaces any existing entry for that name —
	 *        existing consumers' shared_ptrs to the old sampler keep
	 *        working until they drop the reference.
	 */
	SamplerPtr createSampler(const std::string &name, const wgpu::SamplerDescriptor &desc);

	/// Default repeat / linear sampler.
	SamplerPtr getDefaultSampler() { return getSampler(SamplerNames::DEFAULT); }
	/// Mipmap generation sampler (clamp to edge, linear, single mip).
	SamplerPtr getMipmapSampler() { return getSampler(SamplerNames::MIPMAP_LINEAR); }
	/// Clamp-to-edge linear sampler.
	SamplerPtr getClampLinearSampler() { return getSampler(SamplerNames::CLAMP_LINEAR); }
	/// Clamp-to-edge nearest sampler (no filtering).
	SamplerPtr getClampNearestSampler() { return getSampler(SamplerNames::CLAMP_NEAREST); }
	/// Repeat / linear sampler (same as default by convention).
	SamplerPtr getRepeatLinearSampler() { return getSampler(SamplerNames::REPEAT_LINEAR); }
	/// Comparison sampler for shadow PCF (clamp-to-edge, linear, LessEqual).
	SamplerPtr getShadowComparisonSampler() { return getSampler(SamplerNames::SHADOW_COMPARISON); }

	/// Drop every slot AND every build_fn. Used on shutdown / device loss.
	/// Outstanding shared_ptr<WebGPUSampler> consumers keep their samplers
	/// alive via RAII; this only drops the factory's strong refs.
	void cleanup() { m_cache.cleanup(); }

	/// Soft-clear: drop every slot's wgpu sampler but keep slot + build_fn
	/// alive. Next `getSampler(name)` rebuilds via the captured descriptor.
	/// Outstanding consumer shared_ptrs continue to reference the OLD
	/// sampler (RAII keeps it alive until they drop); the next call into
	/// the factory mints a fresh wgpu sampler from the same descriptor.
	/// Use this from "Clear All" UI flows where you want a visible reload
	/// without breaking outstanding consumer references.
	void softClear() { m_cache.clearResources(); }

	// --- CacheRegistry surface ------------------------------------------------
	[[nodiscard]] std::size_t cacheSize() const { return m_cache.cacheSize(); }
	[[nodiscard]] std::size_t aliveCount() const { return m_cache.aliveCount(); }
	void                      notifyFrame() { m_cache.notifyFrame(); }
	std::size_t               evictStale() { return m_cache.evictStale(); }
	void                      setMaxIdleFrames(uint32_t frames) { m_cache.setMaxIdleFrames(frames); }
	[[nodiscard]] uint32_t    maxIdleFrames() const { return m_cache.maxIdleFrames(); }

  private:
	/// Build the wgpu sampler for a known name. Captured by each slot's
	/// build_fn so age-eviction or `clearResources()` can rebuild on demand.
	SamplerPtr buildKnown(const std::string &name);

	wgpu::SamplerDescriptor describeDefault() const;
	wgpu::SamplerDescriptor describeMipmap() const;
	wgpu::SamplerDescriptor describeClampLinear() const;
	wgpu::SamplerDescriptor describeClampNearest() const;
	wgpu::SamplerDescriptor describeRepeatLinear() const;
	wgpu::SamplerDescriptor describeShadowComparison() const;

	WebGPUContext                                                 &m_context;
	engine::rendering::cache::SlotCache<std::string, WebGPUSampler> m_cache;
};
} // namespace engine::rendering::webgpu
