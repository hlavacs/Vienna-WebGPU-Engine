#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <ostream>
#include <stdexcept>
#include <type_traits>

#define ENUM_BEGIN_WRAPPED(Context, COUNT, ...)                                    \
	struct Context                                                                 \
	{                                                                              \
		Context() = delete;                                                        \
		enum class Type                                                            \
		{                                                                          \
			__VA_ARGS__                                                            \
		};                                                                         \
		static constexpr const char *names[COUNT] = {#__VA_ARGS__};                \
		static constexpr size_t size() { return static_cast<std::size_t>(COUNT); } \
                                                                                   \
		static const char *toString(Type value)                                    \
		{                                                                          \
			return names[static_cast<std::size_t>(value)];                         \
		}                                                                          \
                                                                                   \
		static Type fromString(const char *str)                                    \
		{                                                                          \
			auto it = std::find(std::begin(names), std::end(names), str);          \
			if (it != std::end(names))                                             \
				return static_cast<Type>(std::distance(std::begin(names), it));    \
			throw std::invalid_argument("Unknown enum string");                    \
		}

#define ENUM_END() \
	}              \
	;

#define ENUM_BIT_OPERATORS(Context)                                                                                                             \
	inline constexpr Context operator|(Context a, Context b) noexcept                                                                           \
	{                                                                                                                                           \
		return static_cast<Context>(static_cast<std::underlying_type<Context>::type>(a) | static_cast<std::underlying_type<Context>::type>(b)); \
	}                                                                                                                                           \
	inline constexpr Context operator&(Context a, Context b) noexcept                                                                           \
	{                                                                                                                                           \
		return static_cast<Context>(static_cast<std::underlying_type<Context>::type>(a) & static_cast<std::underlying_type<Context>::type>(b)); \
	}                                                                                                                                           \
	inline constexpr Context operator~(Context a) noexcept                                                                                      \
	{                                                                                                                                           \
		return static_cast<Context>(~static_cast<std::underlying_type<Context>::type>(a));                                                      \
	}                                                                                                                                           \
	inline constexpr Context &operator|=(Context &a, Context b) noexcept                                                                        \
	{                                                                                                                                           \
		a = a | b;                                                                                                                              \
		return a;                                                                                                                               \
	}                                                                                                                                           \
	inline constexpr Context &operator&=(Context &a, Context b) noexcept                                                                        \
	{                                                                                                                                           \
		a = a & b;                                                                                                                              \
		return a;                                                                                                                               \
	}

#define ENUM_BIT_FLAGS_HAS(EnumName)                                                                                                     \
	inline static bool hasFlag(EnumName value, EnumName flag) noexcept                                                                 \
	{                                                                                                                                   \
		return (static_cast<std::underlying_type<EnumName>::type>(value) & static_cast<std::underlying_type<EnumName>::type>(flag)) != 0; \
	}

#define ENUM_BIT_OPERATORS_WRAPPED(Context, EnumName)                                                                                                                         \
	inline constexpr Context::EnumName operator|(Context::EnumName a, Context::EnumName b) noexcept                                                                           \
	{                                                                                                                                                                         \
		return static_cast<Context::EnumName>(static_cast<std::underlying_type<Context::EnumName>::type>(a) | static_cast<std::underlying_type<Context::EnumName>::type>(b)); \
	}                                                                                                                                                                         \
	inline constexpr Context::EnumName operator&(Context::EnumName a, Context::EnumName b) noexcept                                                                           \
	{                                                                                                                                                                         \
		return static_cast<Context::EnumName>(static_cast<std::underlying_type<Context::EnumName>::type>(a) & static_cast<std::underlying_type<Context::EnumName>::type>(b)); \
	}                                                                                                                                                                         \
	inline constexpr Context::EnumName operator~(Context::EnumName a) noexcept                                                                                                \
	{                                                                                                                                                                         \
		return static_cast<Context::EnumName>(~static_cast<std::underlying_type<Context::EnumName>::type>(a));                                                                \
	}                                                                                                                                                                         \
	inline constexpr Context::EnumName &operator|=(Context::EnumName &a, Context::EnumName b) noexcept                                                                        \
	{                                                                                                                                                                         \
		a = a | b;                                                                                                                                                            \
		return a;                                                                                                                                                             \
	}                                                                                                                                                                         \
	inline constexpr Context::EnumName &operator&=(Context::EnumName &a, Context::EnumName b) noexcept                                                                        \
	{                                                                                                                                                                         \
		a = a & b;                                                                                                                                                            \
		return a;                                                                                                                                                             \
	}

#define ENUM_BIT_FLAGS_WRAPPED(Context, EnumName, COUNT, ...) \
	struct Context                                            \
	{                                                         \
		Context() = delete;                                   \
		enum class EnumName : uint32_t                        \
		{                                                     \
			__VA_ARGS__                                       \
		};                                                    \
                                                              \
		static constexpr size_t size() { return COUNT; }      \
		ENUM_BIT_FLAGS_HAS(EnumName)                          \
	};                                                        \
                                                              \
	ENUM_BIT_OPERATORS_WRAPPED(Context, EnumName)

#define ENUM_BIT_FLAGS64_WRAPPED(Context, EnumName, COUNT, ...) \
	struct Context                                              \
	{                                                           \
		Context() = delete;                                     \
		enum class EnumName : uint64_t                          \
		{                                                       \
			__VA_ARGS__                                         \
		};                                                      \
                                                                \
		static constexpr size_t size() { return COUNT; }        \
		ENUM_BIT_FLAGS_HAS(EnumName)                            \
	};                                                          \
                                                                \
	ENUM_BIT_OPERATORS_WRAPPED(Context, EnumName)
