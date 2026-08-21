#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

namespace editor
{

/**
 * @brief One captured log record for the editor's Log panel.
 *
 * `seq` is a monotonic counter assigned on insertion. Since records arrive in
 * time order, sorting by `seq` is the stable "by time" ordering even after the
 * ring buffer prunes old entries.
 */
struct LogEntry
{
	uint64_t seq = 0;
	int level = 0;		///< spdlog::level::level_enum as int (trace=0 .. critical=5)
	std::string time;	///< formatted local HH:MM:SS.mmm, built once at capture
	std::string logger; ///< logger name, shown as the "module" column
	std::string message;
};

/**
 * @brief Thread-safe ring buffer of log records.
 *
 * Shared between the spdlog sink (writer, possibly a background thread) and the
 * editor's Log panel (reader, main thread). Oldest records are dropped once the
 * capacity is exceeded. Trace/debug records live in their OWN smaller ring so
 * per-frame debug spam cannot evict info/warn/error entries within seconds;
 * copyInto merges both rings back into time (seq) order.
 */
class LogStore
{
  public:
	void add(LogEntry entry)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		entry.seq = m_nextSeq++;
		const bool noisy = entry.level < static_cast<int>(spdlog::level::info);
		auto &ring = noisy ? m_noisyEntries : m_entries;
		const std::size_t cap = noisy ? m_maxNoisyEntries : m_maxEntries;
		ring.push_back(std::move(entry));
		while (ring.size() > cap)
			ring.pop_front();
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_entries.clear();
		m_noisyEntries.clear();
	}

	/// Copy the current records into @p out (merged, seq-ordered), reusing its capacity.
	void copyInto(std::vector<LogEntry> &out) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		out.clear();
		out.reserve(m_entries.size() + m_noisyEntries.size());
		auto a = m_entries.begin();
		auto b = m_noisyEntries.begin();
		while (a != m_entries.end() && b != m_noisyEntries.end())
			out.push_back(a->seq < b->seq ? *a++ : *b++);
		out.insert(out.end(), a, m_entries.end());
		out.insert(out.end(), b, m_noisyEntries.end());
	}

  private:
	mutable std::mutex m_mutex;
	std::deque<LogEntry> m_entries;      ///< info and above
	std::deque<LogEntry> m_noisyEntries; ///< trace/debug (per-frame spam)
	uint64_t m_nextSeq = 0;
	std::size_t m_maxEntries = 5000;
	std::size_t m_maxNoisyEntries = 1000;
};

/**
 * @brief spdlog sink that forwards every record into a LogStore.
 */
template <typename Mutex>
class EditorLogSinkT : public spdlog::sinks::base_sink<Mutex>
{
  public:
	explicit EditorLogSinkT(std::shared_ptr<LogStore> store) : m_store(std::move(store)) {}

  protected:
	void sink_it_(const spdlog::details::log_msg &msg) override
	{
		if (!m_store)
			return;
		LogEntry entry;
		entry.level = static_cast<int>(msg.level);
		entry.logger.assign(msg.logger_name.data(), msg.logger_name.size());
		entry.message.assign(msg.payload.data(), msg.payload.size());
		entry.time = formatTime(msg.time);
		m_store->add(std::move(entry));
	}

	void flush_() override {}

  private:
	static std::string formatTime(std::chrono::system_clock::time_point timePoint)
	{
		const std::time_t asTime = std::chrono::system_clock::to_time_t(timePoint);
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &asTime);
#else
		localtime_r(&asTime, &local);
#endif
		const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()) % 1000;
		char buffer[16];
		std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec,
			static_cast<int>(millis.count()));
		return std::string(buffer);
	}

	std::shared_ptr<LogStore> m_store;
};

using EditorLogSink = EditorLogSinkT<std::mutex>;

/**
 * @brief Attach an EditorLogSink (feeding @p store) to every spdlog logger and
 * lower their levels to trace so the panel captures all messages.
 *
 * Call once after engine initialization (so the engine's loggers already exist).
 * The UI filters by level for display; capturing trace keeps every level
 * available. The console will therefore also show lower-level messages.
 */
inline void installEditorLogSink(const std::shared_ptr<LogStore> &store)
{
	auto sink = std::make_shared<EditorLogSink>(store);
	sink->set_level(spdlog::level::trace);

	const auto attach = [&](const std::shared_ptr<spdlog::logger> &logger) {
		if (!logger)
			return;
		logger->sinks().push_back(sink);
#if !defined(__EMSCRIPTEN__)
		// Desktop: capture everything; the panel filters by level. On the web,
		// per-frame trace/debug must never be formatted at all (browser perf).
		if (logger->level() != spdlog::level::trace)
			logger->set_level(spdlog::level::trace);
#endif
	};

	auto defaultLogger = spdlog::default_logger();
	bool defaultSeen = false;
	spdlog::apply_all([&](std::shared_ptr<spdlog::logger> logger) {
		attach(logger);
		if (logger == defaultLogger)
			defaultSeen = true;
	});
	if (!defaultSeen)
		attach(defaultLogger);
}

} // namespace editor
