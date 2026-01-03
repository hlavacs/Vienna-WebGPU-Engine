#pragma once

#include <memory>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <typeinfo>

#ifdef __GNUG__
#include <cxxabi.h>
#endif

namespace engine::debug
{

class Loggable
{
	template <typename... Args>
	using format_string_t = fmt::format_string<Args...>;

  protected:
	virtual ~Loggable() = default;

	// Logging functions
	template <typename... Args>
	void logTrace(format_string_t<Args...> fmt, Args &&...args) const
	{
		if (!m_logger)
			initLogger(demangle(typeid(*this).name()));
		m_logger->trace(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void logDebug(format_string_t<Args...> fmt, Args &&...args) const
	{
		if (!m_logger)
			initLogger(demangle(typeid(*this).name()));
		m_logger->debug(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void logInfo(format_string_t<Args...> fmt, Args &&...args) const
	{
		if (!m_logger)
			initLogger(demangle(typeid(*this).name()));
		m_logger->info(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void logWarn(format_string_t<Args...> fmt, Args &&...args) const
	{
		if (!m_logger)
			initLogger(demangle(typeid(*this).name()));
		m_logger->warn(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void logError(format_string_t<Args...> fmt, Args &&...args) const
	{
		if (!m_logger)
			initLogger(demangle(typeid(*this).name()));
		m_logger->error(fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void logCritical(format_string_t<Args...> fmt, Args &&...args) const
	{
		if (!m_logger)
			initLogger(demangle(typeid(*this).name()));
		m_logger->critical(fmt, std::forward<Args>(args)...);
	}

  private:
	void initLogger(const std::string &name) const
	{
		auto existing = spdlog::get(name);
		if (existing)
			m_logger = existing;
		else
			m_logger = spdlog::stdout_color_mt(name);
	}
	mutable std::shared_ptr<spdlog::logger> m_logger;

	/**
	 * @brief Demangles a C++ type name to a human-readable form.
	 * @param name The mangled type name.
	 * @return The demangled type name as a std::string.
	 * @note Uses __cxa_demangle on GCC/Clang, returns original name on other compilers.
	 */
	static std::string demangle(const char *name)
	{
#ifdef __GNUG__
		int status = 0;
		std::unique_ptr<char, void (*)(void *)> res{
			abi::__cxa_demangle(name, nullptr, nullptr, &status),
			std::free
		};
		std::string demangled = (status == 0) ? res.get() : name;
#else
		std::string demangled = name;
#endif

		// Strip namespace: take substring after last '::'
		auto pos = demangled.rfind("::");
		if (pos != std::string::npos)
			return demangled.substr(pos + 2); // +2 to skip '::'

		return demangled;
	}
};

} // namespace engine::debug
