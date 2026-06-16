#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <vector>

namespace engine::rendering::cache
{

/**
 * @brief Type-erased view onto a `ResourceCache<...>` for cross-factory
 *        operations.
 *
 * Factories register a `CacheView` describing the operations the registry
 * needs (frame tick, clean, clear, stats). Each method is a small function
 * pointer + opaque cache pointer — no virtual dispatch, no shared base
 * class on the cache itself. Lets `ResourceCache` stay a pure template
 * with no inheritance, while still giving the renderer a single handle to
 * pump every cache at end-of-frame.
 *
 * Lifetime: the registry stores `CacheView`s by value, but the underlying
 * cache pointer is non-owning. Each factory must outlive its registration
 * (or call unregister) — typically the registry is owned by `WebGPUContext`
 * and lives as long as every factory does, so this is naturally safe.
 */
struct CacheView
{
	void *cache = nullptr;
	const char *label = nullptr;            ///< For debug output. Non-owning string.

	void (*notifyFrame)(void *)    = nullptr;
	std::size_t (*clean)(void *)   = nullptr;
	void (*clear)(void *)          = nullptr;
	/// "Soft clear": drop resources but keep slots + build_fns. Optional;
	/// nullptr means the cache only supports the hard `clear` semantic.
	/// The Clear All UI flow prefers softClear when available — it's safe
	/// mid-frame and lets outstanding handles auto-rebuild on next access
	/// instead of going dead.
	void        (*softClear)(void *) = nullptr;
	std::size_t (*size)(void *)    = nullptr;
	std::size_t (*alive)(void *)   = nullptr;
	// Per-cache lifetime configuration. setMaxIdleFrames(N) configures how
	// long an unaccessed entry may sit in the cache before evictStale()
	// drops it. 0 = never evict by age. getMaxIdleFrames reads it back for
	// debug overlays.
	void     (*setMaxIdleFrames)(void *, uint32_t) = nullptr;
	uint32_t (*getMaxIdleFrames)(void *)           = nullptr;
};

/**
 * @brief Single point for the renderer to drive every cache.
 *
 * Owned by `WebGPUContext`. Factories register their `ResourceCache`s on
 * construction and unregister on destruction. The renderer calls
 * `notifyFrameAll()` at end-of-frame, optionally `cleanAll()` to evict
 * tombstones, and `clearAll()` on hard resets (resize, scene change,
 * device loss).
 */
class CacheRegistry
{
  public:
	CacheRegistry() = default;

	CacheRegistry(const CacheRegistry &)            = delete;
	CacheRegistry &operator=(const CacheRegistry &) = delete;

	/// Register a cache. Templated helper below is the convenient form;
	/// this one is the low-level entry that just stores a view.
	void registerCache(CacheView view);

	/// Drop the registration for @p cache (raw pointer match). No-op if
	/// the cache wasn't registered. Safe to call from cache destructors.
	void unregisterCache(void *cache);

	/// Bump the frame counter on every registered cache. Atomic, lock-free
	/// per cache — safe to call from the renderer's frame loop without
	/// blocking concurrent getOrCreate calls.
	void notifyFrameAll();

	/// Walk every cache, drop tombstones + age-evicted entries. Returns the
	/// total count of entries removed. Heavier than notifyFrameAll —
	/// renderer can choose to skip cleans on busy frames.
	std::size_t cleanAll();

	/// Clear every cache. External shared_ptrs continue to work; the caches
	/// just forget about them. Use on resize, scene change, device loss.
	void clearAll();

	/// Soft-clear every cache that supports it (drops resources but keeps
	/// slots + build_fns so outstanding Handles auto-rebuild on next
	/// access). Caches that only support hard `clear` fall through to it.
	/// This is the verb the Clear All UI flow calls — visible reload
	/// without going dead on outstanding handles. Returns the number of
	/// caches that were soft-cleared (the rest were hard-cleared).
	std::size_t softClearAll();

	/// Total entries across every registered cache (alive + tombstones).
	std::size_t totalSize() const;

	/// Total alive entries across every registered cache. Walks every entry
	/// — debug overlays only.
	std::size_t totalAlive() const;

	std::size_t registeredCount() const;

	/// Convenience overload: build a CacheView from a typed
	/// `ResourceCache<K, R, H>` and register it. Defined in the inline
	/// template below so headers don't pull in ResourceCache.
	template <typename CacheT>
	void registerTypedCache(CacheT &cache, const char *label);

	/// Registration shortcut for engine factories that expose the uniform
	/// cache surface: cleanup(), cacheSize(), notifyFrame(), evictStale(),
	/// setMaxIdleFrames(uint32_t), maxIdleFrames(). Every WebGPU factory in
	/// the engine implements this contract — BaseWebGPUFactory provides it
	/// for the inheritance hierarchy, PipelineManager and SamplerFactory
	/// implement it by hand.
	template <typename FactoryT>
	void registerFactoryCache(FactoryT &factory, const char *label);

	/// Apply a uniform max-idle-frames window to every registered cache.
	/// Use as a global default; per-factory overrides via the by-label
	/// setter below.
	void setMaxIdleFramesForAll(uint32_t frames);

	/// Set the max-idle window for one cache by its registration label.
	/// Returns true if a match was found, false otherwise.
	bool setMaxIdleFramesFor(const char *label, uint32_t frames);

	/// Read back the current max-idle window for one cache by label.
	/// Returns 0 if no match (also the "never evict" sentinel — check
	/// existence via the const overloads below if you need to distinguish).
	[[nodiscard]] uint32_t maxIdleFramesFor(const char *label) const;

	/// Per-cache snapshot for debug overlays. Each row is independent —
	/// dump from a snapshot under one mutex acquire then iterate freely.
	struct Snapshot
	{
		const char *label          = nullptr;
		std::size_t size           = 0;   ///< total entries (alive + tombstones for weak-ref caches)
		std::size_t alive          = 0;   ///< entries that still have a live resource
		uint32_t    maxIdleFrames  = 0;   ///< current eviction window (0 = never)
	};

	/// Take a snapshot of every registered cache. One mutex acquire +
	/// per-cache function-pointer calls — cheap enough to call once per
	/// frame from a debug panel.
	[[nodiscard]] std::vector<Snapshot> snapshotAll() const;

  private:
	mutable std::mutex      m_mutex;
	std::vector<CacheView>  m_views;
};

namespace detail
{
// SFINAE: does `T::softClear()` compile? Used so registerXxxCache can wire
// the soft-clear function pointer only for caches that actually expose it,
// without forcing every cache to declare softClear.
template <typename T, typename = void>
struct HasSoftClear : std::false_type {};

template <typename T>
struct HasSoftClear<T, std::void_t<decltype(std::declval<T &>().softClear())>>
    : std::true_type {};
} // namespace detail

template <typename CacheT>
void CacheRegistry::registerTypedCache(CacheT &cache, const char *label)
{
	CacheView v{};
	v.cache       = static_cast<void *>(&cache);
	v.label       = label;
	v.notifyFrame = [](void *p) { static_cast<CacheT *>(p)->notifyFrame(); };
	v.clean       = [](void *p) -> std::size_t { return static_cast<CacheT *>(p)->clean(); };
	v.clear       = [](void *p) { static_cast<CacheT *>(p)->clear(); };
	v.size        = [](void *p) -> std::size_t { return static_cast<CacheT *>(p)->size(); };
	v.alive       = [](void *p) -> std::size_t { return static_cast<CacheT *>(p)->aliveCount(); };
	if constexpr (detail::HasSoftClear<CacheT>::value)
	{
		v.softClear = [](void *p) { static_cast<CacheT *>(p)->softClear(); };
	}
	registerCache(v);
}

template <typename FactoryT>
void CacheRegistry::registerFactoryCache(FactoryT &factory, const char *label)
{
	CacheView v{};
	v.cache             = static_cast<void *>(&factory);
	v.label             = label;
	v.notifyFrame       = [](void *p) { static_cast<FactoryT *>(p)->notifyFrame(); };
	v.clean             = [](void *p) -> std::size_t { return static_cast<FactoryT *>(p)->evictStale(); };
	v.clear             = [](void *p) { static_cast<FactoryT *>(p)->cleanup(); };
	v.size              = [](void *p) -> std::size_t { return static_cast<FactoryT *>(p)->cacheSize(); };
	v.alive             = [](void *p) -> std::size_t { return static_cast<FactoryT *>(p)->cacheSize(); };
	v.setMaxIdleFrames  = [](void *p, uint32_t f) { static_cast<FactoryT *>(p)->setMaxIdleFrames(f); };
	v.getMaxIdleFrames  = [](void *p) -> uint32_t { return static_cast<FactoryT *>(p)->maxIdleFrames(); };
	if constexpr (detail::HasSoftClear<FactoryT>::value)
	{
		v.softClear = [](void *p) { static_cast<FactoryT *>(p)->softClear(); };
	}
	registerCache(v);
}

} // namespace engine::rendering::cache
