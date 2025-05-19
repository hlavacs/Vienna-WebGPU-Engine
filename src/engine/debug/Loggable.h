

#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace engine::debug
{
	class Loggable
	{
		template <typename... Args>
		using format_string_t = fmt::format_string<Args...>;

	public:
		explicit Loggable(std::shared_ptr<spdlog::logger> logger = nullptr) : m_logger(logger) {}

		virtual ~Loggable() = default;

	protected:
		template <typename... Args>
		inline void logTrace(format_string_t<Args...> fmt, Args &&...args)
		{
			if (m_logger)
			{
				m_logger->trace(fmt, std::forward<Args>(args)...);
			}
		}

		template <typename... Args>
		inline void logDebug(format_string_t<Args...> fmt, Args &&...args)
		{
			if (m_logger)
			{
				m_logger->debug(fmt, std::forward<Args>(args)...);
			}
		}

		template <typename... Args>
		inline void logInfo(format_string_t<Args...> fmt, Args &&...args)
		{
			if (m_logger)
			{
				m_logger->info(fmt, std::forward<Args>(args)...);
			}
		}

		template <typename... Args>
		inline void logWarn(format_string_t<Args...> fmt, Args &&...args)
		{
			if (m_logger)
			{
				m_logger->warn(fmt, std::forward<Args>(args)...);
			}
		}

		template <typename... Args>
		inline void logError(format_string_t<Args...> fmt, Args &&...args)
		{
			if (m_logger)
			{
				m_logger->error(fmt, std::forward<Args>(args)...);
			}
		}

		template <typename... Args>
		inline void logCritical(format_string_t<Args...> fmt, Args &&...args)
		{
			if (m_logger)
			{
				m_logger->critical(fmt, std::forward<Args>(args)...);
			}
		}

		std::shared_ptr<spdlog::logger> m_logger;
	};
}