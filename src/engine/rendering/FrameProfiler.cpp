#include "engine/rendering/FrameProfiler.h"

#include <cstring>

#include <spdlog/spdlog.h>

#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering
{

void FrameProfiler::beginFrame()
{
	// Drain completed GPU readbacks first so the table sees the freshest values.
	pollGpuTimestamps();

	// Reset the timestamp allocator for the new frame's pairs.
	m_nextTimestamp = 0;
	m_currentFramePairs.clear();
}

FrameProfiler::Sample *FrameProfiler::findOrCreate(const std::string &label)
{
	for (auto &s : m_samples)
	{
		if (s.label == label)
			return &s;
	}
	Sample fresh;
	fresh.label = label;
	m_samples.push_back(std::move(fresh));
	return &m_samples.back();
}

void FrameProfiler::record(const std::string &label, float ms)
{
	auto *s = findOrCreate(label);
	s->historyCpu[s->cursorCpu] = ms;
	s->cursorCpu = (s->cursorCpu + 1) % ROLLING_WINDOW;
	if (s->countCpu < ROLLING_WINDOW)
		++s->countCpu;
	s->lastCpuMs = ms;
}

void FrameProfiler::recordGpu(const std::string &label, float ms)
{
	auto *s = findOrCreate(label);
	s->historyGpu[s->cursorGpu] = ms;
	s->cursorGpu = (s->cursorGpu + 1) % ROLLING_WINDOW;
	if (s->countGpu < ROLLING_WINDOW)
		++s->countGpu;
	s->lastGpuMs = ms;
}

std::vector<FrameProfiler::Entry> FrameProfiler::getEntries() const
{
	std::vector<Entry> out;
	out.reserve(m_samples.size());
	for (const auto &s : m_samples)
	{
		Entry e;
		e.label = s.label;
		e.lastCpuMs = s.lastCpuMs;
		e.lastGpuMs = s.lastGpuMs;
		auto avg = [](const auto &history, uint32_t count) {
			if (count == 0) return 0.0f;
			float sum = 0.0f;
			for (uint32_t i = 0; i < count; ++i)
				sum += history[i];
			return sum / static_cast<float>(count);
		};
		e.averageCpuMs = avg(s.historyCpu, s.countCpu);
		e.averageGpuMs = avg(s.historyGpu, s.countGpu);
		out.push_back(std::move(e));
	}
	return out;
}

float FrameProfiler::getTotalCpuMs() const
{
	float total = 0.0f;
	for (const auto &s : m_samples)
		total += s.lastCpuMs;
	return total;
}

void FrameProfiler::clear()
{
	m_samples.clear();
}

// ---------------------------------------------------------------------------
// GPU timing
// ---------------------------------------------------------------------------

void FrameProfiler::initGpu(webgpu::WebGPUContext &context, uint32_t maxPasses)
{
	if (!context.supportsTimestampQuery())
	{
		spdlog::warn("FrameProfiler: device lacks 'timestamp-query' feature; GPU timing disabled");
		m_gpuEnabled = false;
		return;
	}

	m_maxTimestamps = maxPasses * 2;
	const uint64_t bufferSize = static_cast<uint64_t>(m_maxTimestamps) * TIMESTAMP_BYTES;

	wgpu::QuerySetDescriptor qsDesc{};
	qsDesc.label = "FrameProfiler.QuerySet";
	qsDesc.type = wgpu::QueryType::Timestamp;
	qsDesc.count = m_maxTimestamps;
	m_querySet = context.createQuerySet(qsDesc);

	wgpu::BufferDescriptor resDesc{};
	resDesc.label = "FrameProfiler.Resolve";
	resDesc.size = bufferSize;
	resDesc.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
	resDesc.mappedAtCreation = false;
	m_resolveBuffer = context.bufferFactory().createBuffer(resDesc);

	for (auto &f : m_frames)
	{
		wgpu::BufferDescriptor rbDesc{};
		rbDesc.label = "FrameProfiler.Readback";
		rbDesc.size = bufferSize;
		rbDesc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
		rbDesc.mappedAtCreation = false;
		f.readback = context.bufferFactory().createBuffer(rbDesc);
	}

	m_gpuEnabled = true;
	spdlog::info("FrameProfiler: GPU timing enabled ({} timestamp slots, {} in-flight frames)",
		m_maxTimestamps, IN_FLIGHT);
}

bool FrameProfiler::beginGpuScope(const std::string &label, wgpu::CommandEncoder encoder)
{
	if (!m_gpuEnabled || !encoder)
		return false;
	if (m_nextTimestamp + 2 > m_maxTimestamps)
	{
		spdlog::warn("FrameProfiler: timestamp slots exhausted ({} max), dropping '{}'",
			m_maxTimestamps, label);
		return false;
	}
	const uint32_t beginIdx = m_nextTimestamp++;
	const uint32_t endIdx = m_nextTimestamp++;
	encoder.writeTimestamp(m_querySet, beginIdx);
	m_currentFramePairs.push_back({label, beginIdx, endIdx});
	return true;
}

void FrameProfiler::endGpuScope(const std::string &label, wgpu::CommandEncoder encoder)
{
	if (!m_gpuEnabled || !encoder)
		return;
	// Find the matching open pair (last entry with this label that hasn't been
	// closed). Pairs are short-lived (one frame), so a linear scan from the end
	// is cheap.
	for (auto it = m_currentFramePairs.rbegin(); it != m_currentFramePairs.rend(); ++it)
	{
		if (it->label == label)
		{
			encoder.writeTimestamp(m_querySet, it->endIdx);
			return;
		}
	}
	spdlog::warn("FrameProfiler::endGpuScope: no matching beginGpuScope for '{}'", label);
}

void FrameProfiler::resolveGpuTimestamps(webgpu::WebGPUContext &context)
{
	if (!m_gpuEnabled || m_currentFramePairs.empty())
		return;

	auto &slot = m_frames[m_writeFrame];
	if (slot.inFlight)
	{
		// Previous use of this slot hasn't been read yet. Drop this frame's
		// resolves to avoid trampling the in-flight readback. Will recover
		// once mapAsync completes and pollGpuTimestamps consumes it.
		return;
	}

	const uint32_t writtenTimestamps = m_nextTimestamp;
	const uint64_t byteCount = static_cast<uint64_t>(writtenTimestamps) * TIMESTAMP_BYTES;

	wgpu::CommandEncoder encoder = context.createCommandEncoder("FrameProfiler.Resolve");
	encoder.resolveQuerySet(m_querySet, 0, writtenTimestamps, m_resolveBuffer, 0);
	encoder.copyBufferToBuffer(m_resolveBuffer, 0, slot.readback, 0, byteCount);
	context.submitCommandEncoder(encoder, "FrameProfiler.Resolve");

	slot.pairs = m_currentFramePairs;
	slot.inFlight = true;
	slot.mapped = false;

	// Lambda captures slot by reference; m_frames is a std::array member so
	// the reference is pointer-stable for the profiler's lifetime. The
	// returned unique_ptr MUST be stored or the callback object frees and
	// the GPU callback fires through a dangling userdata pointer.
	slot.mapCallback = slot.readback.mapAsync(
		wgpu::MapMode::Read,
		0,
		byteCount,
		[&slot](wgpu::BufferMapAsyncStatus status)
		{
			if (status == wgpu::BufferMapAsyncStatus::Success)
				slot.mapped = true;
			else
				slot.inFlight = false;
		}
	);

	m_writeFrame = (m_writeFrame + 1) % IN_FLIGHT;
}

void FrameProfiler::pollGpuTimestamps()
{
	if (!m_gpuEnabled)
		return;

	for (auto &slot : m_frames)
	{
		if (!slot.inFlight || !slot.mapped)
			continue;

		const auto *data = static_cast<const uint64_t *>(
			slot.readback.getConstMappedRange(0, slot.pairs.size() * 2 * TIMESTAMP_BYTES)
		);
		if (data)
		{
			for (const auto &pair : slot.pairs)
			{
				const uint64_t beginNs = data[pair.beginIdx];
				const uint64_t endNs = data[pair.endIdx];
				if (endNs >= beginNs)
				{
					const float ms = static_cast<float>(endNs - beginNs) * 1e-6f;
					recordGpu(pair.label, ms);
				}
			}
		}
		slot.readback.unmap();
		slot.inFlight = false;
		slot.mapped = false;
		slot.pairs.clear();
		slot.mapCallback.reset();
	}
}

} // namespace engine::rendering
