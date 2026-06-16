#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include "engine/core/Identifiable.h"
#include "engine/rendering/cache/SlotCache.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

/**
 * @brief Templated base class for every WebGPU asset factory.
 *
 * Backed by `cache::SlotCache<Handle, Product>`. The cache stores one slot
 * per source handle; each slot carries a `build_fn` lambda that calls
 * `createFromHandleUncached` on demand. This single substrate gives every
 * factory the same shape:
 *
 *  - **Get-or-create:** `createFromHandle(handle)` looks up the slot, runs
 *    the build_fn on miss, returns a materialised `shared_ptr<Product>`.
 *    Subsequent calls with the same handle hand out the cached pointer
 *    (cheap — one map lookup + one shared_ptr copy).
 *  - **Auto-rebuild after eviction:** the build_fn lives on the slot
 *    forever, so `evictStale()` (age-based) and `softClear()` (UI-driven)
 *    can drop the resource pointer and have the next access transparently
 *    rebuild from the same source handle. No call site has to refetch.
 *  - **CacheRegistry surface:** `notifyFrame`, `evictStale`, `cacheSize`,
 *    `cleanup`, `setMaxIdleFrames`/`maxIdleFrames`, plus a new
 *    `softClear` for the "drop resources, keep slots" semantic the Clear
 *    All button wants. All one-liner delegates to the SlotCache member.
 *  - **Visible "Clear All":** `softClear()` is what the UI should call —
 *    it drops every resource pointer (so GPU memory and downstream
 *    bind groups release their refs) but leaves the slots alive. Next
 *    consumer call rebuilds via the captured build_fn. Hard `cleanup()`
 *    is reserved for shutdown / device loss; using it on a live engine
 *    leaves outstanding handles with no path to rebuild.
 *
 * **Public API contract preserved** — callers still get `shared_ptr<Product>`
 * from `createFromHandle` / `createFrom` / `get`, so adopting this new
 * backing requires zero changes outside the factory implementations.
 *
 * @tparam SourceT  Logical source type. Must derive from
 *                  `engine::core::Identifiable<SourceT>` so its `Handle`
 *                  is a stable, hashable key.
 * @tparam ProductT GPU-side product type held as `std::shared_ptr<ProductT>`.
 */
template <typename SourceT, typename ProductT>
class BaseWebGPUFactory
{
  public:
	using HandleT       = typename SourceT::Handle;
	using ProductPtr    = std::shared_ptr<ProductT>;
	using ResourceCache = engine::rendering::cache::SlotCache<HandleT, ProductT, std::hash<HandleT>>;
	using ResourceHandle = engine::rendering::cache::Handle<ProductT>;

	explicit BaseWebGPUFactory(WebGPUContext &context) :
		m_context(context)
	{
		static_assert(
			std::is_base_of_v<engine::core::Identifiable<SourceT>, SourceT>,
			"SourceT must derive from engine::core::Identifiable<SourceT>");
	}

	virtual ~BaseWebGPUFactory()
	{
		// SlotCache::cleanup() (also called by its destructor) resets every
		// captured build_fn before the slot dies, so any outstanding Handle
		// that lock()s after this factory is destroyed safely returns
		// nullptr instead of dereferencing a dead `this`.
		cleanup();
	}

	/**
	 * @brief Lookup-only: returns the cached product or nullptr. Does NOT
	 *        build on miss. Refreshes the slot's last-access stamp on hit,
	 *        so age-eviction's idle timer restarts.
	 */
	ProductPtr get(const HandleT &handle)
	{
		auto h = m_cache.find(handle);
		return h ? h.lock() : nullptr;
	}

	/// True iff a slot exists for @p handle (regardless of whether its
	/// resource is currently materialised).
	bool has(const HandleT &handle)
	{
		return static_cast<bool>(m_cache.find(handle));
	}

	/**
	 * @brief Convenience: build a Handle from a Source, then forward to
	 *        createFromHandle.
	 *
	 * @throws std::runtime_error if @p source has no registered handle.
	 */
	ProductPtr createFrom(const SourceT &source)
	{
		HandleT handle{};
		try
		{
			const engine::core::Identifiable<SourceT> &identifiable =
				static_cast<const engine::core::Identifiable<SourceT> &>(source);
			handle = identifiable.getHandle();
		}
		catch (const std::exception &e)
		{
			throw std::runtime_error(
				std::string("Could not create handle: ") + e.what()
				+ "\nA valid handle is required. Make sure the source object is registered.");
		}
		return createFromHandle(handle);
	}

	/**
	 * @brief Get-or-create the GPU product for @p handle.
	 *
	 * Cache hit returns the existing shared_ptr. Cache miss calls the
	 * captured build_fn (which delegates to `createFromHandleUncached`) and
	 * stores the result. Returns nullptr only if the build fails.
	 */
	virtual ProductPtr createFromHandle(const HandleT &handle)
	{
		return m_cache.getOrCreate(handle, [this, handle]() {
			return createFromHandleUncached(handle);
		}).lock();
	}

	/**
	 * @brief Drop every slot AND every build_fn.
	 *
	 * Outstanding consumer shared_ptrs keep their resources alive via RAII;
	 * outstanding Handles to slots-no-longer-in-the-cache lock() to nullptr
	 * (no build_fn left). Use only on shutdown / device loss.
	 */
	virtual void cleanup() { m_cache.cleanup(); }

	/**
	 * @brief Drop every slot's resource but keep slots + build_fns alive.
	 *
	 * This is the verb the "Clear All" UI wants: visible GPU memory
	 * pressure drops, downstream bind groups release their refs, and the
	 * next consumer call rebuilds the resource lazily via the captured
	 * build_fn. Consumer-held shared_ptrs from earlier calls keep working
	 * until they're dropped, which is what makes this safe to invoke
	 * mid-frame.
	 */
	void softClear() { m_cache.clearResources(); }

	// --- CacheRegistry surface -----------------------------------------------
	[[nodiscard]] std::size_t cacheSize() const { return m_cache.cacheSize(); }
	[[nodiscard]] std::size_t aliveCount() const { return m_cache.aliveCount(); }
	void                       notifyFrame() { m_cache.notifyFrame(); }
	std::size_t                evictStale() { return m_cache.evictStale(); }
	void                       setMaxIdleFrames(uint32_t frames) { m_cache.setMaxIdleFrames(frames); }
	[[nodiscard]] uint32_t     maxIdleFrames() const { return m_cache.maxIdleFrames(); }

  protected:
	/// Subclasses implement the actual GPU-side construction. Called by the
	/// slot's build_fn on cache miss and after eviction; the result is
	/// stored in a new slot (initial build) or replaces the current
	/// resource (after eviction-then-lock).
	virtual ProductPtr createFromHandleUncached(const HandleT &handle) = 0;

	WebGPUContext &m_context;
	ResourceCache  m_cache;
};

} // namespace engine::rendering::webgpu
