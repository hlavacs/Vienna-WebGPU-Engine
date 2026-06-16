#include "engine/rendering/cache/CacheRegistry.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "engine/rendering/cache/ResourceCache.h"
#include "engine/rendering/cache/ResourceSlot.h"

namespace engine::rendering::cache
{

namespace
{
// Compile-time exercise of every public ResourceCache / ResourceSlot /
// Handle surface. No runtime effect — purely there so a syntax slip in
// the headers is caught by the engine library build instead of leaking
// into the first downstream factory that tries to instantiate the
// templates.
struct CacheCompileProbe
{
	struct R {};
	[[maybe_unused]] static void run()
	{
		// Weak-ref cache — for memory-pressure-sensitive use cases like
		// material instances.
		ResourceCache<int, R> c;
		c.setConfig({1, 16});
		(void)c.config();
		auto r = c.getOrCreate(1, []() { return std::make_shared<R>(); });
		(void)r;
		c.notifyFrame();
		(void)c.clean();
		(void)c.size();
		(void)c.aliveCount();
		(void)c.stats();
		c.invalidate(1);
		c.invalidateIf([](const int &) { return false; });
		c.clear();

		// Slot + Handle — for hot-reloadable resources where the factory
		// keeps strong refs and callers hold opaque handles. Exercise the
		// full surface: build_fn auto-rebuild, frame-counter tracking,
		// evict/replace + the snapshot semantics.
		std::atomic<uint32_t> frameSrc{0};
		auto slot = std::make_shared<ResourceSlot<R>>(
			std::make_shared<R>(),
			[]() { return std::make_shared<R>(); },
			&frameSrc);
		Handle<R> h{slot};
		(void)h.valid();
		(void)h.lock();
		(void)h.version();
		(void)slot->isAlive();
		(void)slot->peek();
		(void)slot->lastAccessFrame();
		auto prev = slot->replace(std::make_shared<R>());
		(void)prev;
		auto old = slot->evict();
		(void)old;
		(void)h.lock();  // triggers auto-rebuild via build_fn
		slot->resetBuildFn();
		(void)slot->version();
		Handle<R> empty;
		(void)empty.valid();
		(void)(h == empty);
	}
};
} // namespace


void CacheRegistry::registerCache(CacheView view)
{
	if (!view.cache) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	// Duplicate registration is a bug — the same cache being driven twice
	// per frame would double-bump its frame counter. Cheap O(n) guard.
	for (const auto &v : m_views)
	{
		if (v.cache == view.cache) return;
	}
	m_views.push_back(view);
}

void CacheRegistry::unregisterCache(void *cache)
{
	if (!cache) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_views.erase(
		std::remove_if(m_views.begin(), m_views.end(),
		               [cache](const CacheView &v) { return v.cache == cache; }),
		m_views.end());
}

void CacheRegistry::notifyFrameAll()
{
	// Snapshot under lock; call notifyFrame outside so concurrent
	// registration during the loop can't deadlock with cache->mutex.
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	for (const auto &v : snapshot)
	{
		if (v.notifyFrame) v.notifyFrame(v.cache);
	}
}

std::size_t CacheRegistry::cleanAll()
{
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	std::size_t total = 0;
	for (const auto &v : snapshot)
	{
		if (v.clean) total += v.clean(v.cache);
	}
	return total;
}

void CacheRegistry::clearAll()
{
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	for (const auto &v : snapshot)
	{
		if (v.clear) v.clear(v.cache);
	}
}

std::size_t CacheRegistry::softClearAll()
{
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	std::size_t softCount = 0;
	for (const auto &v : snapshot)
	{
		if (v.softClear)
		{
			v.softClear(v.cache);
			++softCount;
		}
		else if (v.clear)
		{
			// No soft-clear available — fall through to hard clear for any
			// legacy cache that hasn't migrated. Consumer-held shared_ptrs
			// keep resources alive across the call; the cache just forgets
			// it had them.
			v.clear(v.cache);
		}
	}
	return softCount;
}

std::size_t CacheRegistry::totalSize() const
{
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	std::size_t total = 0;
	for (const auto &v : snapshot)
	{
		if (v.size) total += v.size(v.cache);
	}
	return total;
}

std::size_t CacheRegistry::totalAlive() const
{
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	std::size_t total = 0;
	for (const auto &v : snapshot)
	{
		if (v.alive) total += v.alive(v.cache);
	}
	return total;
}

std::size_t CacheRegistry::registeredCount() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_views.size();
}

void CacheRegistry::setMaxIdleFramesForAll(uint32_t frames)
{
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	for (const auto &v : snapshot)
	{
		if (v.setMaxIdleFrames) v.setMaxIdleFrames(v.cache, frames);
	}
}

bool CacheRegistry::setMaxIdleFramesFor(const char *label, uint32_t frames)
{
	if (!label) return false;
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	for (const auto &v : snapshot)
	{
		if (v.label && std::string_view(v.label) == label && v.setMaxIdleFrames)
		{
			v.setMaxIdleFrames(v.cache, frames);
			return true;
		}
	}
	return false;
}

uint32_t CacheRegistry::maxIdleFramesFor(const char *label) const
{
	if (!label) return 0;
	std::vector<CacheView> snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		snapshot = m_views;
	}
	for (const auto &v : snapshot)
	{
		if (v.label && std::string_view(v.label) == label && v.getMaxIdleFrames)
		{
			return v.getMaxIdleFrames(v.cache);
		}
	}
	return 0;
}

std::vector<CacheRegistry::Snapshot> CacheRegistry::snapshotAll() const
{
	std::vector<CacheView> views;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		views = m_views;
	}
	std::vector<Snapshot> out;
	out.reserve(views.size());
	for (const auto &v : views)
	{
		Snapshot s{};
		s.label         = v.label;
		s.size          = v.size  ? v.size(v.cache)  : 0;
		s.alive         = v.alive ? v.alive(v.cache) : 0;
		s.maxIdleFrames = v.getMaxIdleFrames ? v.getMaxIdleFrames(v.cache) : 0;
		out.push_back(s);
	}
	return out;
}

} // namespace engine::rendering::cache
