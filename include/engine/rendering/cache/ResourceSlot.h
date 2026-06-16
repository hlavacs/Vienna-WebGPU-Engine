#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace engine::rendering::cache
{

/**
 * @brief One indirection slot owning a hot-swappable resource, optionally with
 *        an age-based eviction + auto-rebuild policy.
 *
 * A Slot is the unit a factory stores in its cache. Externally, callers hold
 * `Handle<T>` (a `shared_ptr<ResourceSlot<T>>`); they never see the resource
 * pointer directly. The factory keeps a strong ref to the slot — callers'
 * `lock()` calls go through the slot via one mutex acquire + one shared_ptr
 * copy. No map lookup per access.
 *
 * **Three operations factories use on slots:**
 *  1. `replace(new)` — hot reload. The new resource is swapped in; bumps
 *     version. Existing pinned snapshots (held by callers via lock())
 *     continue pointing at the OLD resource until they're dropped, so
 *     in-flight GPU work is safe.
 *  2. `evict()` — release the resource pointer but keep the slot alive.
 *     Next `lock()` either rebuilds (if a build_fn was provided at slot
 *     construction) or returns nullptr. Pinned snapshots from previous
 *     lock() calls keep working.
 *  3. `lock()` — caller-facing. Updates the last-access frame counter so
 *     the factory's age-eviction pass knows the slot is still in use.
 *     Auto-rebuilds via build_fn if the resource is gone but a build_fn
 *     was set; otherwise returns nullptr. Snapshot semantics: the returned
 *     shared_ptr stays valid even if `evict()` or `replace()` happens
 *     between lock() and the caller's next access.
 *
 * **Auto-rebuild on eviction.** Factories construct slots with a build_fn
 * that knows how to recreate the resource (typically a lambda capturing
 * the factory pointer + key). When the factory evicts a stale slot, the
 * resource is destroyed; the slot stays alive in the factory map and in
 * any outstanding Handles. The next caller `lock()` triggers a transparent
 * rebuild via build_fn. From the caller's perspective the handle "always
 * works" — they don't have to refetch from the factory ever.
 *
 * **"Throw on missing" mode** is achieved by NOT passing a build_fn: a
 * caller that wants strict "fail loud if the factory evicted me" can
 * construct slots with no build_fn and check `lock()` for nullptr.
 *
 * **Frame counter.** Slots optionally hold a non-owning pointer to a
 * factory-owned `atomic<uint32_t>` frame counter. `lock()` reads it and
 * stamps `lastAccessFrame`. Factory's eviction pass compares against
 * its own current frame to decide what to evict.
 *
 * **Thread safety.** All ops take an internal mutex. `lock()` calls
 * build_fn while holding the mutex — build_fn must not recursively call
 * back into the same slot (or take any lock that's already held). Slot
 * construction is also thread-safe: the slot's atomics + members are
 * fully initialised before the shared_ptr<Slot> is published to the
 * factory's map.
 */
template <typename T>
class ResourceSlot
{
  public:
	using ResourcePtr = std::shared_ptr<T>;
	using BuildFn     = std::function<ResourcePtr()>;

	ResourceSlot() = default;

	/// Factory-style constructor: initial resource + optional build_fn for
	/// auto-rebuild + optional pointer to a factory-owned frame counter
	/// (for age-based eviction tracking).
	ResourceSlot(ResourcePtr initial,
	             BuildFn     build       = nullptr,
	             const std::atomic<uint32_t> *frameSource = nullptr)
		: m_resource(std::move(initial)),
		  m_build(std::move(build)),
		  m_frameSource(frameSource)
	{
		if (m_frameSource)
		{
			m_lastAccessFrame.store(m_frameSource->load(std::memory_order_relaxed),
			                        std::memory_order_relaxed);
		}
	}

	ResourceSlot(const ResourceSlot &)            = delete;
	ResourceSlot &operator=(const ResourceSlot &) = delete;

	/// Pin a snapshot of the current resource. Updates lastAccessFrame
	/// from the factory's frame counter so age-eviction knows we're still
	/// using this. If the resource was evicted and a build_fn is set,
	/// rebuilds transparently before returning. Returns nullptr only if
	/// the slot was never populated AND no build_fn is set.
	ResourcePtr lock()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		stampLastAccess_locked();
		if (m_resource)
			return m_resource;
		if (m_build)
		{
			m_resource = m_build();
			return m_resource;
		}
		return nullptr;
	}

	/// Const overload that does NOT trigger rebuild and does NOT bump the
	/// last-access counter — pure observer for debug overlays / diagnostics.
	ResourcePtr peek() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_resource;
	}

	/// Replace the resource. All future `lock()` calls return @p next;
	/// callers that already captured a snapshot keep using the old
	/// resource until they next call lock(). Bumps the version. Returns
	/// the previous resource (caller may want to keep it alive until
	/// end-of-frame to be safe across in-flight GPU work).
	ResourcePtr replace(ResourcePtr next)
	{
		ResourcePtr previous;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			previous = std::move(m_resource);
			m_resource = std::move(next);
		}
		m_version.fetch_add(1, std::memory_order_release);
		return previous;
	}

	/// Release the resource pointer but keep the slot (and its build_fn)
	/// alive. The next `lock()` either rebuilds via build_fn or returns
	/// nullptr. Outstanding pinned snapshots from earlier `lock()` calls
	/// still hold the old resource alive via their own shared_ptr — so
	/// evict() never crashes in-flight GPU work.
	ResourcePtr evict()
	{
		ResourcePtr previous;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			previous = std::move(m_resource);
		}
		// Don't bump version — eviction is not a "the resource changed
		// semantically" event; downstream caches don't need to invalidate.
		return previous;
	}

	/// Reset the build_fn — used by factories on destruction to break
	/// captured `this` pointers in build_fn before the factory dies.
	/// Outstanding handles that lock() after this point will get nullptr
	/// instead of dereferencing a destroyed factory.
	void resetBuildFn()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_build = nullptr;
	}

	/// Monotonic counter, bumped on every `replace(...)`. Use for
	/// downstream caches that need to know "did my dependency change?".
	uint32_t version() const { return m_version.load(std::memory_order_acquire); }

	/// Last frame the resource was accessed via lock(). Factories compare
	/// this against their current frame to decide what to evict.
	uint32_t lastAccessFrame() const
	{
		return m_lastAccessFrame.load(std::memory_order_acquire);
	}

	/// True if the slot currently holds a resource (i.e. lock() would NOT
	/// need to rebuild). Cheaper than peek() because it doesn't pin a
	/// shared_ptr.
	bool isAlive() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return static_cast<bool>(m_resource);
	}

	/// Deprecated alias for isAlive() — keeps existing valid() callers
	/// compiling while the codebase migrates.
	bool valid() const { return isAlive(); }

  private:
	void stampLastAccess_locked()
	{
		if (m_frameSource)
		{
			m_lastAccessFrame.store(m_frameSource->load(std::memory_order_relaxed),
			                        std::memory_order_relaxed);
		}
	}

	mutable std::mutex          m_mutex;
	ResourcePtr                 m_resource;
	BuildFn                     m_build;            ///< Optional auto-rebuild on lock.
	const std::atomic<uint32_t>* m_frameSource = nullptr; ///< Non-owning.
	std::atomic<uint32_t>       m_version{0};
	std::atomic<uint32_t>       m_lastAccessFrame{0};
};

/**
 * @brief Caller-facing handle to a slot owned by a factory.
 *
 * Construct-by-value, copy/move trivially. Calling `lock()` returns the
 * current resource snapshot from the slot (rebuilding if needed). The
 * snapshot is stable for the lifetime of the returned shared_ptr even if
 * the factory hot-reloads or evicts in the meantime.
 *
 * Default-constructed Handles are empty; querying them returns nullptr.
 */
template <typename T>
class Handle
{
  public:
	using SlotPtr     = std::shared_ptr<ResourceSlot<T>>;
	using ResourcePtr = std::shared_ptr<T>;

	Handle() = default;
	explicit Handle(SlotPtr slot) : m_slot(std::move(slot)) {}

	bool valid() const { return static_cast<bool>(m_slot) && m_slot->isAlive(); }
	explicit operator bool() const { return valid(); }

	/// True iff this Handle is bound to a slot (even if the slot's resource
	/// is currently evicted). Distinct from `valid()` / `operator bool()`,
	/// which additionally require the slot's resource to be materialised.
	/// Use this for "have I already fetched a handle for this key?" cache
	/// fills — `valid()` would force a re-fetch every frame after a
	/// soft-clear, since the slot is intact but the resource is null.
	[[nodiscard]] bool isAttached() const { return static_cast<bool>(m_slot); }

	/// Pin a snapshot of the current resource. Updates the slot's
	/// last-access counter so the factory knows we're still using it.
	/// If the resource was evicted and the slot has a build_fn, rebuilds
	/// transparently. Returns nullptr if the slot is empty (only happens
	/// when no build_fn was set and someone evicted, or for empty handles).
	///
	/// The returned shared_ptr is a snapshot — even if `replace()` or
	/// `evict()` is called on the slot between this call and the next, the
	/// caller's pinned ref keeps the resource alive for the duration of
	/// their use.
	ResourcePtr lock() const { return m_slot ? m_slot->lock() : nullptr; }

	/// Convenience — UB if the handle is empty. Use sparingly; prefer
	/// pinning a `lock()` snapshot for a draw call.
	T *operator->() const { return lock().get(); }
	T &operator*()  const { return *lock(); }

	/// Version of the underlying slot. Use to detect reload across frames.
	uint32_t version() const { return m_slot ? m_slot->version() : 0; }

	/// Compare by slot identity, NOT by resource value — two handles are
	/// equal iff they point at the same factory cache entry.
	bool operator==(const Handle &other) const { return m_slot == other.m_slot; }
	bool operator!=(const Handle &other) const { return m_slot != other.m_slot; }

  private:
	SlotPtr m_slot;
};

} // namespace engine::rendering::cache
