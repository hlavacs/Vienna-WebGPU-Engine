#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
}

namespace engine::rendering
{

/**
 * @class FrameProfiler
 * @brief Per-pass CPU timing with rolling averages.
 *
 * Scoped: push a label with `beginScope("PassName")` before the work and pop
 * with `endScope()` after. `beginFrame()` rotates the buffer so a new frame's
 * samples accumulate without losing the rolling average over the last N.
 *
 * Reads the smoothed result via `getEntries()` - vector of {label, cpu_ms}
 * rounded to a few decimal places, sorted in insertion order so the call site
 * controls the display order.
 *
 * CPU-only for now. GPU timing needs a separate timestamp-query path that
 * requires the `timestamp-query` device feature and an async resolve/readback.
 */
class FrameProfiler
{
public:
	static constexpr size_t ROLLING_WINDOW = 60; ///< Samples averaged per entry.

	/**
	 * @brief Mark the start of a new frame's measurements.
	 * Stable label set across frames produces meaningful rolling averages -
	 * inserting a new label mid-session adds it to the average from that point on.
	 */
	void beginFrame();

	/**
	 * @brief Record a single sample directly. Cheap path for callers that
	 * already have a duration in hand. Most code should use @ref Scope below.
	 */
	void record(const std::string &label, float milliseconds);

	/**
	 * @brief RAII wrapper. Self-contained start time so nested scopes (e.g.
	 * Frame.Total wrapping every per-pass scope) all measure correctly.
	 */
	struct Scope
	{
		FrameProfiler &p;
		std::string label;
		std::chrono::steady_clock::time_point start;
		Scope(FrameProfiler &profiler, std::string lab) :
			p(profiler),
			label(std::move(lab)),
			start(std::chrono::steady_clock::now())
		{
		}
		~Scope()
		{
			auto end = std::chrono::steady_clock::now();
			p.record(label, std::chrono::duration<float, std::milli>(end - start).count());
		}
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;
	};

	struct Entry
	{
		std::string label;
		float averageCpuMs; ///< Mean of the last ROLLING_WINDOW CPU samples.
		float lastCpuMs;	///< Most recent single-frame CPU sample.
		float averageGpuMs; ///< Mean of the last ROLLING_WINDOW GPU samples (0 if GPU timing disabled).
		float lastGpuMs;	///< Most recent GPU sample.
	};

	/**
	 * @brief Snapshot of every label seen across the last ROLLING_WINDOW frames,
	 *        in the order they were first registered.
	 */
	[[nodiscard]] std::vector<Entry> getEntries() const;

	/**
	 * @brief Rolling-average total of every scope's most recent frame.
	 * Useful as a "CPU work per frame" summary independent of vsync wait.
	 */
	[[nodiscard]] float getTotalCpuMs() const;

	void clear();

	// ---------------------------------------------------------------------
	// GPU timing via WebGPU timestamp queries
	// ---------------------------------------------------------------------

	/**
	 * @brief One-time setup of the GPU timing path. No-op (and disables GPU
	 * timing) when the device wasn't created with the `timestamp-query` feature.
	 * @param maxPasses Maximum distinct labels timed per frame. Each label
	 *                  costs two timestamp slots; the query set is sized for 2x.
	 */
	void initGpu(webgpu::WebGPUContext &context, uint32_t maxPasses = 32);

	[[nodiscard]] bool isGpuTimingEnabled() const { return m_gpuEnabled; }

	/**
	 * @brief Allocate a {begin, end} timestamp pair for the given label and
	 * write the begin timestamp on @p encoder. Pair with @ref endGpuScope.
	 * @return true if a pair was issued (false if GPU timing is disabled or full).
	 */
	bool beginGpuScope(const std::string &label, wgpu::CommandEncoder encoder);

	/**
	 * @brief Write the end timestamp for @p label on @p encoder and queue the
	 * pair for resolve/readback at frame end.
	 */
	void endGpuScope(const std::string &label, wgpu::CommandEncoder encoder);

	/**
	 * @brief Resolve every recorded timestamp pair into a mappable buffer for
	 * async readback. Called by the renderer once per frame, after every pass
	 * submission but before present.
	 */
	void resolveGpuTimestamps(webgpu::WebGPUContext &context);

	/**
	 * @brief Poll completed readbacks and feed durations into the rolling
	 * averages. Called at frame start.
	 */
	void pollGpuTimestamps();

private:
	struct Sample
	{
		std::string label;
		std::array<float, ROLLING_WINDOW> historyCpu{};
		std::array<float, ROLLING_WINDOW> historyGpu{};
		uint32_t countCpu = 0;
		uint32_t cursorCpu = 0;
		uint32_t countGpu = 0;
		uint32_t cursorGpu = 0;
		float lastCpuMs = 0.0f;
		float lastGpuMs = 0.0f;
	};

	[[nodiscard]] Sample *findOrCreate(const std::string &label);
	void recordGpu(const std::string &label, float ms);

	std::vector<Sample> m_samples;

	// ----- GPU timing state -----
	struct PendingPair
	{
		std::string label;
		uint32_t beginIdx;
		uint32_t endIdx;
	};
	struct InFlightFrame
	{
		std::vector<PendingPair> pairs;
		wgpu::Buffer readback = nullptr;
		// MUST own the unique_ptr returned by mapAsync. The wgpu C++ wrapper
		// stores the callback inside it and passes a raw pointer through the
		// underlying C API as userdata - dropping the unique_ptr immediately
		// frees the callback and the GPU's eventual invocation derefs a
		// dangling pointer (the access violation we hit before).
		std::unique_ptr<wgpu::BufferMapCallback> mapCallback;
		bool inFlight = false;
		bool mapped = false;
	};
	static constexpr uint32_t IN_FLIGHT = 3;     ///< triple-buffered readback
	static constexpr uint32_t TIMESTAMP_BYTES = 8;

	bool m_gpuEnabled = false;
	uint32_t m_maxTimestamps = 0;
	wgpu::QuerySet m_querySet = nullptr;
	wgpu::Buffer m_resolveBuffer = nullptr;
	std::array<InFlightFrame, IN_FLIGHT> m_frames{};
	uint32_t m_writeFrame = 0;
	uint32_t m_nextTimestamp = 0;
	std::vector<PendingPair> m_currentFramePairs;
};

} // namespace engine::rendering
