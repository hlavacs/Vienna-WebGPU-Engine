#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

#include "engine/core/Identifiable.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

/**
 * @brief Templated base class for all WebGPU factories.
 *
 * Adds the **uniform cache lifecycle surface** every cached factory in the
 * engine exposes:
 *  - `cleanup()` — clear the entire cache (resize / scene reset / device loss)
 *  - `cacheSize()` — entry count (debug overlays)
 *  - `notifyFrame()` — bump frame counter (called by CacheRegistry once per frame)
 *  - `evictStale()` — drop entries whose lastAccessFrame is more than
 *    `maxIdleFrames` behind the current frame (called periodically by
 *    CacheRegistry)
 *  - `setMaxIdleFrames(N)` / `maxIdleFrames()` — per-factory eviction window;
 *    0 = "keep forever" (default for safety; the WebGPUContext init sets a
 *    real default per-factory).
 *
 * The cache stores entries as `{resource, lastAccessFrame}`. Every `get()`
 * / `createFromHandle()` cache hit updates `lastAccessFrame`, so a frequently
 * touched entry never times out. Note: only factory calls refresh the
 * timestamp — once a caller stashes their `shared_ptr<ProductT>`, they own
 * lifetime themselves (the factory's strong ref becoming a "tombstone"
 * just means it'll get rebuilt on the next factory call). This matches
 * what you want for "scene change / run away from object": when no one
 * fetches a resource for N frames, the factory drops its strong ref.
 *
 * @tparam SourceT Type used to create the GPU resource.
 * @tparam ProductT GPU resource type produced by the factory.
 */
template <typename SourceT, typename ProductT>
class BaseWebGPUFactory
{
  public:
	/**
	 * @brief Construct a factory with a WebGPU context.
	 * @param context The WebGPU context used for resource creation.
	 */
	explicit BaseWebGPUFactory(WebGPUContext &context) :
		m_context(context)
	{
		// Static assert that SourceT is derived from Identifiable<SourceT>
		static_assert(std::is_base_of_v<engine::core::Identifiable<SourceT>, SourceT>, "SourceT must derive from engine::core::Identifiable<SourceT>");
	}
	virtual ~BaseWebGPUFactory()
	{
		cleanup();
	};

	/**
	 * @brief Get a GPU resource from a source handle if it exists.
	 * @return Shared pointer to the GPU resource, or nullptr if not found.
	 * @note Refreshes lastAccessFrame on hit so eviction's "idle" timer
	 *       restarts.
	 */
	std::shared_ptr<ProductT> get(const typename SourceT::Handle &handle)
	{
		auto it = m_cache.find(handle);
		if (it != m_cache.end())
		{
			it->second.lastAccessFrame = m_frameCounter.load(std::memory_order_relaxed);
			return it->second.resource;
		}
		return nullptr;
	}

	/**
	 * @brief Check if a GPU resource exists for the given source handle.
	 * @note Pure observer — does NOT refresh lastAccessFrame.
	 */
	bool has(const typename SourceT::Handle &handle)
	{
		return m_cache.find(handle) != m_cache.end();
	}

	/**
	 * @brief Get or create a GPU resource from a source object.
	 * @note This automatically creates a handle from the source and calls createFromHandle.
	 */
	std::shared_ptr<ProductT> createFrom(const SourceT &source)
	{
		typename engine::core::Identifiable<SourceT>::HandleType handle{};
		try
		{
			const engine::core::Identifiable<SourceT> &identifiable = static_cast<const engine::core::Identifiable<SourceT> &>(source);
			handle = identifiable.getHandle();
		}
		catch (const std::exception &e)
		{
			throw std::runtime_error(std::string("Could not create handle: ") + e.what() + "\nA valid handle is required. Make sure the source object is registered.");
		}
		return createFromHandle(handle);
	}

	/**
	 * @brief Get or create a GPU resource from a source handle.
	 * @note Cache hit refreshes lastAccessFrame; cache miss builds a fresh
	 *       resource and inserts it with the current frame as lastAccess.
	 */
	virtual std::shared_ptr<ProductT> createFromHandle(const typename SourceT::Handle &handle)
	{
		const uint32_t now = m_frameCounter.load(std::memory_order_relaxed);
		auto it = m_cache.find(handle);
		if (it != m_cache.end())
		{
			it->second.lastAccessFrame = now;
			return it->second.resource;
		}
		auto product = createFromHandleUncached(handle);
		m_cache.emplace(handle, Entry{product, now});
		return product;
	}

	/**
	 * @brief Clear the internal cache of created resources.
	 * @warning Existing shared_ptrs held by callers still keep their resources
	 *          alive; this only drops the factory's strong refs.
	 */
	virtual void cleanup()
	{
		m_cache.clear();
	}

	/// Total cached entries — for CacheRegistry / debug overlays.
	[[nodiscard]] std::size_t cacheSize() const { return m_cache.size(); }

	/// Bump the frame counter. Called by CacheRegistry::notifyFrameAll once
	/// per frame. Atomic, lock-free.
	void notifyFrame() { m_frameCounter.fetch_add(1, std::memory_order_relaxed); }

	/// Drop cache entries whose lastAccessFrame is more than `maxIdleFrames`
	/// behind the current frame. No-op when `maxIdleFrames == 0` (the
	/// "keep forever" default). Returns count dropped.
	std::size_t evictStale()
	{
		if (m_maxIdleFrames == 0) return 0;
		const uint32_t now = m_frameCounter.load(std::memory_order_relaxed);
		std::size_t evicted = 0;
		for (auto it = m_cache.begin(); it != m_cache.end();)
		{
			// Unsigned subtraction wraps cleanly under m_frameCounter
			// overflow at 2^32 frames (~2 years at 60fps).
			if ((now - it->second.lastAccessFrame) > m_maxIdleFrames)
			{
				it = m_cache.erase(it);
				++evicted;
			}
			else
			{
				++it;
			}
		}
		return evicted;
	}

	/// Configure the age-eviction window. After @p frames of no
	/// get()/createFromHandle access, an entry is dropped on the next
	/// evictStale() sweep. 0 = never evict by age.
	void setMaxIdleFrames(uint32_t frames) { m_maxIdleFrames = frames; }
	[[nodiscard]] uint32_t maxIdleFrames() const { return m_maxIdleFrames; }

  protected:
	/**
	 * @brief Create a GPU resource from a handle to a source object.
	 */
	virtual std::shared_ptr<ProductT> createFromHandleUncached(const typename SourceT::Handle &handle) = 0;

  protected:
	/// One cache entry: resource pointer + last-access frame stamp.
	/// Wrapping in a struct lets every factory share the eviction code path.
	struct Entry
	{
		std::shared_ptr<ProductT> resource;
		uint32_t                  lastAccessFrame = 0;
	};

	WebGPUContext &m_context;

	std::unordered_map<typename SourceT::Handle, Entry> m_cache;

	// Frame counter source — slots/lookups read from this. Atomic so the
	// renderer can tick it without holding a factory mutex.
	std::atomic<uint32_t> m_frameCounter{0};

	uint32_t m_maxIdleFrames = 0;
};

} // namespace engine::rendering::webgpu
