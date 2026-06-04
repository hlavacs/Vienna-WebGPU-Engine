#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace engine::rendering::shaders::aggregate_reflect
{

/**
 * @file AggregateReflect.h
 * @brief Minimal compile-time reflection for plain aggregate structs (C++17).
 *
 * Inspired by boost::pfr; reimplemented in ~120 LoC with no external
 * dependencies. Gives field count, field type at index, and field offset for
 * any plain aggregate (no user-declared constructors, no virtual functions,
 * no protected/private members). Field NAMES are not derivable from C++17 —
 * the user supplies those via `GpuStructTraits<T>::fieldNames`.
 *
 * Supports structs with up to 16 fields. Bump kMaxFields + extend tieAsTuple
 * if a larger struct shows up.
 */

constexpr size_t kMaxFields = 16;

/// SFINAE proxy that pretends to be any field type. Used only inside decltype
/// contexts to test whether `T{AnyType{}, AnyType{}, ...}` is valid.
struct AnyType
{
	template <typename U> constexpr operator U() const noexcept; // declared, not defined
};

namespace detail
{

template <typename T, std::size_t... Is>
constexpr auto isBraceCtorImpl(std::index_sequence<Is...>, int)
	-> decltype(T{(static_cast<void>(Is), AnyType{})...}, std::true_type{})
{
	return {};
}

template <typename T, std::size_t...>
constexpr auto isBraceCtorImpl(...) -> std::false_type
{
	return {};
}

template <typename T, std::size_t N>
constexpr bool isBraceConstructibleWith =
	decltype(isBraceCtorImpl<T>(std::make_index_sequence<N>{}, 0))::value;

template <typename T, std::size_t N>
constexpr std::size_t findFieldCount()
{
	if constexpr (N == 0)
	{
		return 0;
	}
	else if constexpr (isBraceConstructibleWith<T, N>)
	{
		return N;
	}
	else
	{
		return findFieldCount<T, N - 1>();
	}
}

} // namespace detail

/// Compile-time number of fields in aggregate @p T. Walks from kMaxFields
/// downward to find the maximum N for which `T{N args}` is well-formed.
template <typename T>
constexpr std::size_t fieldCount = detail::findFieldCount<T, kMaxFields>();

/// Bind @p value's fields with structured bindings and return them as a
/// tuple of lvalue references. Manual unroll up to kMaxFields — there's no
/// way to do this generically in C++17 (and even C++26 reflection uses an
/// unroll under the hood, just hidden by the language).
template <typename T>
auto tieAsTuple(T &v)
{
	constexpr std::size_t N = fieldCount<T>;
	static_assert(N <= kMaxFields, "Struct exceeds AggregateReflect::kMaxFields; bump the limit");
	if constexpr      (N ==  1) { auto &[a] = v;                                                                                        return std::tie(a); }
	else if constexpr (N ==  2) { auto &[a, b] = v;                                                                                     return std::tie(a, b); }
	else if constexpr (N ==  3) { auto &[a, b, c] = v;                                                                                  return std::tie(a, b, c); }
	else if constexpr (N ==  4) { auto &[a, b, c, d] = v;                                                                               return std::tie(a, b, c, d); }
	else if constexpr (N ==  5) { auto &[a, b, c, d, e] = v;                                                                            return std::tie(a, b, c, d, e); }
	else if constexpr (N ==  6) { auto &[a, b, c, d, e, f] = v;                                                                         return std::tie(a, b, c, d, e, f); }
	else if constexpr (N ==  7) { auto &[a, b, c, d, e, f, g] = v;                                                                      return std::tie(a, b, c, d, e, f, g); }
	else if constexpr (N ==  8) { auto &[a, b, c, d, e, f, g, h] = v;                                                                   return std::tie(a, b, c, d, e, f, g, h); }
	else if constexpr (N ==  9) { auto &[a, b, c, d, e, f, g, h, i] = v;                                                                return std::tie(a, b, c, d, e, f, g, h, i); }
	else if constexpr (N == 10) { auto &[a, b, c, d, e, f, g, h, i, j] = v;                                                             return std::tie(a, b, c, d, e, f, g, h, i, j); }
	else if constexpr (N == 11) { auto &[a, b, c, d, e, f, g, h, i, j, k] = v;                                                          return std::tie(a, b, c, d, e, f, g, h, i, j, k); }
	else if constexpr (N == 12) { auto &[a, b, c, d, e, f, g, h, i, j, k, l] = v;                                                       return std::tie(a, b, c, d, e, f, g, h, i, j, k, l); }
	else if constexpr (N == 13) { auto &[a, b, c, d, e, f, g, h, i, j, k, l, m] = v;                                                    return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m); }
	else if constexpr (N == 14) { auto &[a, b, c, d, e, f, g, h, i, j, k, l, m, n] = v;                                                 return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, n); }
	else if constexpr (N == 15) { auto &[a, b, c, d, e, f, g, h, i, j, k, l, m, n, o] = v;                                              return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o); }
	else if constexpr (N == 16) { auto &[a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p] = v;                                           return std::tie(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p); }
	else                        { static_assert(N == 0, "tieAsTuple cannot handle zero-field aggregates"); return std::tie(); }
}

/// Type of @p T's I-th field, cv/ref stripped.
template <std::size_t I, typename T>
using FieldTypeAt = std::remove_cv_t<std::remove_reference_t<
	std::tuple_element_t<I, decltype(tieAsTuple(std::declval<T &>()))>>>;

/**
 * @brief Runtime byte offset of @p T's I-th field.
 *
 * Computed once per (I, T) pair against a static default-constructed instance.
 * @p T must be default-constructible. Result is cached by static-initialisation.
 */
template <std::size_t I, typename T>
std::size_t fieldOffsetAt()
{
	static const std::size_t cached = []() -> std::size_t {
		static T instance{};
		auto       refs    = tieAsTuple(instance);
		const auto &field  = std::get<I>(refs);
		return static_cast<std::size_t>(
			reinterpret_cast<const char *>(&field) - reinterpret_cast<const char *>(&instance));
	}();
	return cached;
}

} // namespace engine::rendering::shaders::aggregate_reflect
