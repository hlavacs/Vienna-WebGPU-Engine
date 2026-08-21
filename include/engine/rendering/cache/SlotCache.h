#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "engine/rendering/cache/ResourceSlot.h"

namespace engine::rendering::cache
{

/**
 * @brief Generic slot-keyed cache. The one caching primitive every WebGPU
 *        factory should reach for when adding a new cached resource type.
 *
 * Externally, callers hold `Handle<Resource>` (`engine::rendering::cache::Handle`).
 * Internally, the cache stores one `ResourceSlot<Resource>` per key plus the
 * `build_fn` that knows how to rebuild on demand. The slot is the strong
 * owner of the current resource; consumers' `Handle::lock()` returns a
 * pinned snapshot they can use safely across in-flight GPU work even if the
 * resource is later evicted or replaced.
 *
 * **What this primitive collapses into one type:**
 *
 *  1. **Dedup by key.** `getOrCreate(key, build)` returns the same Handle
 *     for the same key across calls. Hot path is one map lookup; the
 *     `build` lambda only runs on a true miss.
 *  2. **Hot reload in place.** `replace(key, next)` swaps the resource
 *     inside the slot. Every outstanding Handle sees `next` on its next
 *     `lock()` call. No call site has to refetch.
 *  3. **Age-based eviction.** `evictStale()` drops the *resource* of any
 *     slot whose Handles haven't `lock()`-touched in `maxIdleFrames`
 *     frames. The slot stays — the next `lock()` rebuilds via the
 *     captured `build_fn`. Snapshots already pinned by callers stay valid.
 *  4. **Visible "Clear All".** `clearResources()` drops every slot's
 *     resource (Handles auto-rebuild on next access) — the verb the user
 *     wants behind a `Clear` button. Distinct from `cleanup()`, which
 *     also drops the `build_fn`s (use only on shutdown / device loss).
 *
 * **Why slots + handles rather than raw `shared_ptr` cache or `weak_ptr`
 * cache:**
 *
 *  - Raw `shared_ptr` cache (the old `BaseWebGPUFactory` model) couples
 *    "the cache knows about this" with "the resource is alive": every
 *    consumer's copy keeps the resource alive, and the cache's strong ref
 *    means `cleanup()` is invisible because consumer refs survive.
 *    Hot-swap is impossible because consumers see only their own copy.
 *  - `weak_ptr` cache (`ResourceCache<K, R>`) is pure dedup but loses
 *    the "factory keeps things between calls" behaviour and can't
 *    propagate hot reloads to consumers' refs at all.
 *  - SlotCache hands out an *indirection* (Handle). The factory can
 *    swap the underlying resource without the consumer noticing, evict
 *    without breaking the consumer, and report "Handle is still
 *    semantically valid" even when the resource has been recycled.
 *
 * **Thread safety.** All ops take the cache mutex. `build_fn` runs under
 * the cache mutex (consistent with `ResourceCache` / `ResourceSlot`).
 * Avoid recursive cache calls inside `build_fn`.
 *
 * **CacheRegistry integration.** SlotCache implements the same six-method
 * surface every other factory does — `notifyFrame`, `evictStale`,
 * `cacheSize`, `cleanup`, `setMaxIdleFrames`, `maxIdleFrames` — so
 * `CacheRegistry::registerFactoryCache(slotCache, label)` works directly
 * on a SlotCache instance, no shim needed.
 *
 * @tparam Key        Lookup key. Hashable + equality-comparable.
 * @tparam Resource   Resource type stored as `std::shared_ptr<Resource>`.
 * @tparam KeyHash    Optional custom hash (defaults to `std::hash<Key>`).
 */
template <typename Key, typename Resource, typename KeyHash = std::hash<Key>>
class SlotCache
{
  public:
	using Slot        = ResourceSlot<Resource>;
	using HandleT     = Handle<Resource>;
	using ResourcePtr = std::shared_ptr<Resource>;
	using BuildFn     = std::function<ResourcePtr()>;

	SlotCache() = default;
	~SlotCache() { cleanup(); }

	SlotCache(const SlotCache &)            = delete;
	SlotCache &operator=(const SlotCache &) = delete;

	/**
	 * @brief Get-or-create a Handle for @p key.
	 *
	 * On miss, runs @p buildFn to produce the initial resource AND stores
	 * @p buildFn as the slot's auto-rebuild function — future evictions
	 * call the same lambda to materialise a fresh resource transparently.
	 * On hit, returns the existing Handle and discards @p buildFn (no
	 * re-binding).
	 *
	 * If @p buildFn returns nullptr on the initial miss, returns an empty
	 * Handle and does not populate the cache. (Failed builds shouldn't
	 * masquerade as cached entries.)
	 */
	HandleT getOrCreate(const Key &key, BuildFn buildFn)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_slots.find(key);
		if (it != m_slots.end()) return HandleT(it->second);

		auto initial = buildFn();
		if (!initial) return HandleT{};

		auto slot = std::make_shared<Slot>(initial, std::move(buildFn), &m_frameCounter);
		m_slots.emplace(key, slot);
		return HandleT(slot);
	}

	/**
	 * @brief Lookup-only. Returns an empty Handle on miss, never builds.
	 */
	HandleT find(const Key &key) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_slots.find(key);
		return it != m_slots.end() ? HandleT(it->second) : HandleT{};
	}

	/**
	 * @brief Replace the resource inside @p key's slot. No-op if @p key is
	 *        not cached. All outstanding Handles see @p next on next
	 *        `lock()`. The previous resource stays alive only as long as
	 *        any pinned snapshot keeps it — safe across in-flight GPU work.
	 */
	void replace(const Key &key, ResourcePtr next)
	{
		std::shared_ptr<Slot> slot;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_slots.find(key);
			if (it == m_slots.end()) return;
			slot = it->second;
		}
		slot->replace(std::move(next));
	}

	/**
	 * @brief Evict @p key's resource; keep the slot + build_fn alive so
	 *        outstanding Handles auto-rebuild on next access. No-op if not
	 *        cached.
	 */
	void invalidate(const Key &key)
	{
		std::shared_ptr<Slot> slot;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_slots.find(key);
			if (it == m_slots.end()) return;
			slot = it->second;
		}
		slot->evict();
	}

	/**
	 * @brief Erase the slot for @p key entirely (slot + build_fn + resource).
	 *
	 * Outstanding Handles for @p key keep working for any snapshot they
	 * already pinned via `lock()`, but their next `lock()` returns nullptr
	 * (no slot, no build_fn). Use this for "really forget this key existed"
	 * semantics — e.g. `unregisterShader`. For "drop the cached resource
	 * but let outstanding Handles auto-rebuild" use `invalidate` instead.
	 *
	 * @return true if the key was cached, false otherwise.
	 */
	bool erase(const Key &key)
	{
		std::shared_ptr<Slot> slot;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_slots.find(key);
			if (it == m_slots.end()) return false;
			slot = it->second;
			m_slots.erase(it);
		}
		// Reset build_fn outside the cache mutex.
		slot->resetBuildFn();
		return true;
	}

	/**
	 * @brief Drop the resource of every slot but keep slots + build_fns.
	 *        Outstanding Handles auto-rebuild on next access.
	 *
	 * This is the visible "Clear All" verb: GPU memory pressure drops,
	 * pipelines and bind groups rebuild lazily, no consumer has to
	 * refetch a handle.
	 */
	void clearResources()
	{
		std::vector<std::shared_ptr<Slot>> snapshot = snapshotSlots();
		for (auto &s : snapshot) s->evict();
	}

	/**
	 * @brief Synchronously rebuild every slot via its build_fn. Returns
	 *        count successfully rebuilt. Failures (build_fn returns null)
	 *        leave the slot's previous resource intact.
	 */
	std::size_t reloadAll()
	{
		std::vector<std::shared_ptr<Slot>> snapshot = snapshotSlots();
		std::size_t rebuilt = 0;
		for (auto &s : snapshot)
		{
			// evict() drops the existing resource, then lock() runs the
			// captured build_fn. The slot's version bumps only on
			// replace() (semantic change), not on lock-after-evict, so
			// downstream version watchers see this as "transparent
			// rebuild" — same as age-eviction's behaviour.
			s->evict();
			if (s->lock()) ++rebuilt;
		}
		return rebuilt;
	}

	/**
	 * @brief Drop every slot AND every build_fn. Outstanding Handles'
	 *        previously-pinned snapshots still work; their next
	 *        `lock()` returns nullptr (no rebuild possible).
	 *
	 * Use on shutdown / device loss. The destructor calls this; manual
	 * use is rare.
	 */
	void cleanup()
	{
		std::vector<std::shared_ptr<Slot>> snapshot;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			snapshot.reserve(m_slots.size());
			for (auto &kv : m_slots) snapshot.push_back(kv.second);
			m_slots.clear();
		}
		// Reset build_fns outside the cache mutex so a build_fn that
		// transitively wants back into the cache can't deadlock.
		for (auto &s : snapshot) s->resetBuildFn();
	}

	/// All keys currently in the cache (snapshot). O(n). Used by the few
	/// consumers that need to iterate (e.g. "mark all for hot reload").
	std::vector<Key> keys() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<Key> out;
		out.reserve(m_slots.size());
		for (auto &kv : m_slots) out.push_back(kv.first);
		return out;
	}

	// --- CacheRegistry surface ------------------------------------------------

	/// Bump frame counter; slots stamp it on `lock()`.
	void notifyFrame() { m_frameCounter.fetch_add(1, std::memory_order_relaxed); }

	/// Drop the resource of slots whose lastAccessFrame is more than
	/// maxIdleFrames behind. No-op when maxIdleFrames == 0. Slots stay alive
	/// so Handles auto-rebuild on next access. Returns count evicted.
	std::size_t evictStale()
	{
		if (m_maxIdleFrames == 0) return 0;
		const uint32_t now = m_frameCounter.load(std::memory_order_relaxed);
		std::vector<std::shared_ptr<Slot>> snapshot = snapshotSlots();
		std::size_t evicted = 0;
		for (auto &s : snapshot)
		{
			if (!s->isAlive()) continue;
			// Unsigned subtraction wraps cleanly under frame-counter overflow
			// at 2^32 frames (~2 years @ 60fps).
			if ((now - s->lastAccessFrame()) > m_maxIdleFrames)
			{
				s->evict();
				++evicted;
			}
		}
		return evicted;
	}

	std::size_t cacheSize() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_slots.size();
	}

	/// Slots whose resource pointer is currently materialised.
	std::size_t aliveCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::size_t alive = 0;
		for (auto &kv : m_slots)
		{
			if (kv.second && kv.second->isAlive()) ++alive;
		}
		return alive;
	}

	void                       setMaxIdleFrames(uint32_t frames) { m_maxIdleFrames = frames; }
	[[nodiscard]] uint32_t     maxIdleFrames() const { return m_maxIdleFrames; }

	/// Non-owning pointer to the frame counter slots stamp on `lock()`.
	/// Exposed mainly so external consumers building slots by hand can
	/// hook into the same frame source — internal `getOrCreate` already
	/// wires this up.
	const std::atomic<uint32_t> *frameSourcePtr() const { return &m_frameCounter; }

  private:
	/// Snapshot of every slot pointer under one mutex acquisition. Lets
	/// per-slot ops run outside the cache mutex so build_fns / replace
	/// callbacks don't deadlock with concurrent getOrCreate calls.
	std::vector<std::shared_ptr<Slot>> snapshotSlots() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<std::shared_ptr<Slot>> out;
		out.reserve(m_slots.size());
		for (auto &kv : m_slots) out.push_back(kv.second);
		return out;
	}

	mutable std::mutex                                           m_mutex;
	std::unordered_map<Key, std::shared_ptr<Slot>, KeyHash>      m_slots;
	std::atomic<uint32_t>                                        m_frameCounter{0};
	uint32_t                                                     m_maxIdleFrames = 0;
};

} // namespace engine::rendering::cache
