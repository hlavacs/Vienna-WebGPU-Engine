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
 * capacity is exceeded.
 */
class LogStore
{
  public:
	void add(LogEntry entry)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		entry.seq = m_nextSeq++;
		m_entries.push_back(std::move(entry));
		while (m_entries.size() > m_maxEntries)
			m_entries.pop_front();
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_entries.clear();
	}

	/// Copy the current records into @p out, reusing its capacity.
	void copyInto(std::vector<LogEntry> &out) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		out.assign(m_entries.begin(), m_entries.end());
	}

  private:
	mutable std::mutex m_mutex;
	std::deque<LogEntry> m_entries;
	uint64_t m_nextSeq = 0;
	std::size_t m_maxEntries = 5000;
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
		if (logger->level() != spdlog::level::trace)
			logger->set_level(spdlog::level::trace);
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
