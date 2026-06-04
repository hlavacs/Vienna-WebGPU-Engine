#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace engine::rendering::cache
{

/**
 * @brief Thread-safe, weak-pointer backed cache for engine factories.
 *
 * @tparam Key       Lookup key. Must be hashable + equality-comparable.
 * @tparam Resource  Cached resource type. Stored as std::weak_ptr<Resource>
 *                   internally; getOrCreate returns std::shared_ptr<Resource>.
 * @tparam KeyHash   Optional custom hasher (defaults to std::hash<Key>).
 *
 * **Lifetime model.**
 *  - Entries are stored as `std::weak_ptr<Resource>` keyed by @p Key.
 *  - `getOrCreate(key, build)`:
 *      * cache miss → calls @p build, stores weak_ptr, returns shared_ptr.
 *      * cache hit & weak_ptr alive → returns shared_ptr from the weak_ptr
 *        without calling @p build. Counts as a hit.
 *      * cache hit & weak_ptr expired → calls @p build, replaces the entry,
 *        returns the new shared_ptr. Counts as a rebuild.
 *  - The cache **never** holds a strong reference. A resource lives only as
 *    long as at least one external owner (the original caller, a render
 *    pass that stashed it, etc.) holds a `shared_ptr`. The moment the last
 *    external owner drops, the resource is destroyed; the cache's
 *    `weak_ptr` becomes a tombstone that's silently rebuilt on next access.
 *
 * This model means **callers MUST hold the returned shared_ptr for as long
 * as they want the resource to remain cached**. Calling getOrCreate every
 * frame without retaining the result defeats the cache entirely (it will
 * rebuild every frame). Cached factory consumers are expected to stash
 * pipelines / bind groups / etc. on their own state.
 *
 * **Optional age-based eviction.**
 *  - `setConfig({.maxIdleFrames = N})` plus periodic `notifyFrame()` calls
 *    drop entries that haven't been accessed in the last N frame ticks,
 *    even if external owners still hold them. The dropped weak_ptr only
 *    removes the cache entry — the resource itself stays alive as long as
 *    the external shared_ptr does.
 *  - `setConfig({.maxEntries = N})` caps total entries; LRU-style eviction
 *    drops the least-recently-accessed entry when inserting over the cap.
 *
 * **Thread safety.**
 *  - All public ops take an internal `std::mutex`. Concurrent
 *    `getOrCreate` / `invalidate` / `clean` / `clear` from multiple threads
 *    is safe. @p build is called *while holding the mutex* — keep it cheap
 *    and non-recursive. (Future: drop the mutex around the build call and
 *    re-check on insert if build cost matters.)
 *  - `notifyFrame()` is atomic (no mutex), so the renderer thread can tick
 *    the frame counter without contending with cache lookups.
 *
 * **Hot-reload tagging (for task #8).** Out of scope for this header —
 * the cache's invalidate predicate is generic enough that a tag system
 * can be layered on by the caller (key includes a tag, predicate filters
 * by tag).
 */
template <typename Key, typename Resource, typename KeyHash = std::hash<Key>>
class ResourceCache
{
  public:
	using ResourcePtr = std::shared_ptr<Resource>;
	using BuildFn     = std::function<ResourcePtr()>;

	struct Config
	{
		/// Drop entries whose last access was more than N frames ago. 0 = never
		/// evict by age (cache only relies on weak_ptr expiry for cleanup).
		uint32_t maxIdleFrames = 0;
		/// Cap on total entries. 0 = unlimited. When exceeded, LRU eviction
		/// drops the oldest-accessed entries down to the cap. Note that
		/// "entries" includes both alive and tombstone weak_ptrs — call
		/// clean() periodically to keep tombstones from inflating the count.
		uint32_t maxEntries = 0;
	};

	struct Stats
	{
		std::size_t hits      = 0;  ///< weak_ptr alive, no rebuild needed
		std::size_t misses    = 0;  ///< key not present
		std::size_t rebuilds  = 0;  ///< key present but weak_ptr expired
		std::size_t evictions = 0;  ///< entries dropped by clean/age/LRU
	};

	ResourceCache() = default;

	// Caches own a mutex; not copyable.
	ResourceCache(const ResourceCache &)            = delete;
	ResourceCache &operator=(const ResourceCache &) = delete;

	/**
	 * @brief Look up @p key; on miss or expired weak_ptr, call @p build to
	 *        produce a new resource and cache it. Returns a strong reference.
	 *        @p build must not be null and must not return nullptr.
	 *
	 * Thread-safe. @p build runs under the cache mutex — keep it short.
	 */
	ResourcePtr getOrCreate(const Key &key, const BuildFn &build)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint32_t now = m_frame.load(std::memory_order_relaxed);

		auto it = m_entries.find(key);
		if (it != m_entries.end())
		{
			if (auto strong = it->second.resource.lock())
			{
				it->second.lastAccessFrame = now;
				++m_stats.hits;
				return strong;
			}
			// Tombstone: weak_ptr expired since last access. Fall through to
			// rebuild but treat it as a rebuild (not a miss) so call-site
			// instrumentation can tell "I needed to rebuild" from "I asked
			// about something the cache had never seen".
			++m_stats.rebuilds;
			ResourcePtr fresh = build();
			it->second.resource        = fresh;
			it->second.lastAccessFrame = now;
			return fresh;
		}

		++m_stats.misses;
		ResourcePtr fresh = build();
		m_entries.emplace(key, Entry{fresh, now});

		// Respect maxEntries cap if configured. Cheap LRU: scan for the
		// least-recently-accessed entry and drop it. Acceptable while the
		// expected cap is small (≤ a few hundred); revisit with a proper LRU
		// list if any consumer needs thousands of entries.
		if (m_config.maxEntries > 0 && m_entries.size() > m_config.maxEntries)
		{
			evictOldest_locked();
		}
		return fresh;
	}

	/// Drop the entry for @p key if present. The underlying resource is
	/// unaffected — external shared_ptrs keep working — but the next
	/// getOrCreate call for this key will go through @p build again.
	void invalidate(const Key &key)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_entries.erase(key) > 0)
		{
			++m_stats.evictions;
		}
	}

	/// Drop all entries matching @p pred. Useful for hot reload
	/// ("invalidate everything tagged with shader X"). @p pred runs under
	/// the cache mutex.
	void invalidateIf(const std::function<bool(const Key &)> &pred)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_entries.begin(); it != m_entries.end();)
		{
			if (pred(it->first))
			{
				it = m_entries.erase(it);
				++m_stats.evictions;
			}
			else
			{
				++it;
			}
		}
	}

	/// Drop all entries. External shared_ptrs continue to work; the cache
	/// just forgets they exist.
	void clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stats.evictions += m_entries.size();
		m_entries.clear();
	}

	/// Remove tombstones (entries whose weak_ptr is expired) and entries
	/// that have exceeded maxIdleFrames. Safe to call from any thread; the
	/// renderer's idea of "end of frame" is a natural call site. Returns
	/// the count of entries removed.
	std::size_t clean()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint32_t now = m_frame.load(std::memory_order_relaxed);
		const bool     ageCheck = m_config.maxIdleFrames > 0;

		std::size_t removed = 0;
		for (auto it = m_entries.begin(); it != m_entries.end();)
		{
			const bool expired = it->second.resource.expired();
			const bool stale   = ageCheck
			    && (now - it->second.lastAccessFrame) > m_config.maxIdleFrames;
			if (expired || stale)
			{
				it = m_entries.erase(it);
				++removed;
			}
			else
			{
				++it;
			}
		}
		m_stats.evictions += removed;
		return removed;
	}

	/// Increment the internal frame counter. Drives maxIdleFrames eviction.
	/// Atomic, no mutex — safe to call from any thread.
	void notifyFrame() { m_frame.fetch_add(1, std::memory_order_relaxed); }

	void setConfig(Config c)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config = c;
	}

	Config config() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_config;
	}

	Stats stats() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_stats;
	}

	/// Total entries (alive + tombstones). Use clean() first for a strict
	/// "currently cached" count.
	std::size_t size() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_entries.size();
	}

	/// Entries whose weak_ptr is still alive. Walks the entire map under
	/// the lock — use sparingly (debug overlays / tests, not hot paths).
	std::size_t aliveCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::size_t alive = 0;
		for (const auto &kv : m_entries)
		{
			if (!kv.second.resource.expired()) ++alive;
		}
		return alive;
	}

  private:
	struct Entry
	{
		std::weak_ptr<Resource> resource;
		uint32_t                lastAccessFrame = 0;
	};

	/// Caller must hold m_mutex. Drops the least-recently-accessed entry.
	/// O(n); only called when maxEntries cap is exceeded.
	void evictOldest_locked()
	{
		auto oldest = m_entries.begin();
		for (auto it = std::next(m_entries.begin()); it != m_entries.end(); ++it)
		{
			if (it->second.lastAccessFrame < oldest->second.lastAccessFrame)
			{
				oldest = it;
			}
		}
		if (oldest != m_entries.end())
		{
			m_entries.erase(oldest);
			++m_stats.evictions;
		}
	}

	mutable std::mutex                              m_mutex;
	std::unordered_map<Key, Entry, KeyHash>         m_entries;
	std::atomic<uint32_t>                           m_frame{0};
	Config                                          m_config{};
	mutable Stats                                   m_stats{};
};

} // namespace engine::rendering::cache
