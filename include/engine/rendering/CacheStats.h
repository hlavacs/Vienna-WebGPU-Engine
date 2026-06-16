#pragma once

#include <atomic>
#include <cstdint>

namespace engine::rendering
{

/**
 * @brief Cumulative skip / execute counters for the per-frame fingerprint
 *        caches scattered across the renderer (cluster compute, scene-
 *        light upload, frustum cull, scene bind-group rebuild, etc.).
 *
 * Each counter is a global `std::atomic<uint64_t>` that the producing
 * code bumps unconditionally when it decides to skip vs. actually do
 * the work. The Performance debug window reads them out and shows
 * skip-rate percentages — the only direct runtime signal that the
 * optimisations we landed are actually firing on the current workload.
 *
 * **Why globals.** These counters are pure debug instrumentation. There
 * is exactly one engine running at a time in the demo; threading the
 * stats through a context would just be ceremony. The atomics make the
 * unconditional bumps safe for any thread that might end up doing
 * render work (we don't multi-thread it yet, but the cost is one
 * relaxed atomic op per bump — invisible in any profile).
 *
 * **Reset semantics**: counters are cumulative across the session.
 * Reset by writing zero to every counter from the UI (`reset()`).
 * That gives a "how is THIS scenario behaving?" measurement without
 * polluting from earlier scene loads or camera motion.
 */
namespace CacheStats
{

inline std::atomic<uint64_t> clusterDispatchesExecuted{0};
inline std::atomic<uint64_t> clusterDispatchesSkipped{0};

inline std::atomic<uint64_t> sceneLightUploadsExecuted{0};
inline std::atomic<uint64_t> sceneLightUploadsSkipped{0};

inline std::atomic<uint64_t> frustumCullsExecuted{0};
inline std::atomic<uint64_t> frustumCullsSkipped{0};

inline std::atomic<uint64_t> sceneBindGroupRebuilds{0};
inline std::atomic<uint64_t> sceneBindGroupHits{0};

inline std::atomic<uint64_t> skyboxBindGroupRebuilds{0};
inline std::atomic<uint64_t> skyboxBindGroupHits{0};

/// Objects whose `prepareGPUResources` fast path matched the cached slot
/// (no rebuild) vs. the slow path (factory lookups + bind group + writeBuffer).
inline std::atomic<uint64_t> objectsFastPath{0};
inline std::atomic<uint64_t> objectsSlowPath{0};

/// Of the fast-path objects, how many actually fired a queue.writeBuffer
/// because the world transform moved (vs. truly static = zero buffer cost).
inline std::atomic<uint64_t> objectTransformWrites{0};

/// Reset every counter to zero. Use from the UI when you want a clean
/// measurement window for a specific scenario.
inline void reset()
{
	clusterDispatchesExecuted.store(0, std::memory_order_relaxed);
	clusterDispatchesSkipped.store(0, std::memory_order_relaxed);
	sceneLightUploadsExecuted.store(0, std::memory_order_relaxed);
	sceneLightUploadsSkipped.store(0, std::memory_order_relaxed);
	frustumCullsExecuted.store(0, std::memory_order_relaxed);
	frustumCullsSkipped.store(0, std::memory_order_relaxed);
	sceneBindGroupRebuilds.store(0, std::memory_order_relaxed);
	sceneBindGroupHits.store(0, std::memory_order_relaxed);
	skyboxBindGroupRebuilds.store(0, std::memory_order_relaxed);
	skyboxBindGroupHits.store(0, std::memory_order_relaxed);
	objectsFastPath.store(0, std::memory_order_relaxed);
	objectsSlowPath.store(0, std::memory_order_relaxed);
	objectTransformWrites.store(0, std::memory_order_relaxed);
}

} // namespace CacheStats

} // namespace engine::rendering
