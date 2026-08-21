#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <type_traits>

#include "engine/rendering/shaders/AggregateReflect.h"
#include "engine/rendering/shaders/EnginePreprocessor.h"
#include "engine/rendering/shaders/StructDescriptor.h"
#include "engine/rendering/shaders/WgslTypes.h"

namespace engine::rendering::shaders
{

/**
 * @brief Per-struct trait carrying the bits C++ reflection can't give us:
 *        the WGSL struct name and the per-field WGSL names.
 *
 * The convention is **tokens = field names**. Use the `ENGINE_GPU_STRUCT`
 * macro to declare a trait spec; the macro stringifies each C++ field name
 * verbatim, so WGSL ends up with the same camelCase names as the C++ struct:
 *
 *   ENGINE_GPU_STRUCT(engine::rendering::FrameUniforms,
 *       viewMatrix,
 *       projectionMatrix,
 *       viewProjectionMatrix,
 *       cameraWorldPosition,
 *       time)
 *
 * Types and offsets are picked up automatically from the C++ struct via
 * `aggregate_reflect`. The macro expansion is therefore the *only* per-struct
 * boilerplate; no `offsetof`, no `WgslType` per field, no manual specialisation
 * boilerplate.
 *
 * Manual specialisations (no macro) work too — just declare `wgslName` and a
 * `fieldNames[]` C-array; the consumer only requires those two static members.
 */
template <typename T> struct GpuStructTraits; // primary, undefined: specialise per T

/// Compile-time count of `GpuStructTraits<T>::fieldNames` entries. Works on
/// both C-array (from the macro) and explicitly-sized arrays.
template <typename T>
constexpr std::size_t traitsFieldCount()
{
	return sizeof(GpuStructTraits<T>::fieldNames) / sizeof(GpuStructTraits<T>::fieldNames[0]);
}

namespace detail
{

template <typename T, std::size_t... Is>
StructDescriptor buildDescriptorFromTraits(std::index_sequence<Is...>)
{
	using Traits = GpuStructTraits<T>;
	return StructDescriptor::build<T>(
		Traits::wgslName,
		std::initializer_list<StructField>{
			StructField{
				Traits::fieldNames[Is],
				wgslTypeOf<aggregate_reflect::FieldTypeAt<Is, T>>(),
				static_cast<uint32_t>(aggregate_reflect::fieldOffsetAt<Is, T>())
			}...
		});
}

} // namespace detail

/**
 * @brief Lazily-built `StructDescriptor` for any T that has a
 *        `GpuStructTraits<T>` specialisation. Cached by static initialisation.
 *
 * Validates at compile time that `fieldCount<T>` equals the trait's field
 * count — a struct change that doesn't get mirrored in the traits spec
 * fails the build instead of producing a wrong descriptor.
 */
template <typename T>
const StructDescriptor &gpuStructDescriptorOf()
{
	static_assert(std::is_aggregate_v<T>,
		"gpuStructDescriptorOf<T>: T must be an aggregate type (plain struct)");
	static_assert(aggregate_reflect::fieldCount<T> == traitsFieldCount<T>(),
		"GpuStructTraits<T>::fieldNames count must equal the C++ struct's field count");
	static const StructDescriptor d =
		detail::buildDescriptorFromTraits<T>(std::make_index_sequence<aggregate_reflect::fieldCount<T>>{});
	return d;
}

} // namespace engine::rendering::shaders

namespace engine::rendering::shaders
{

/// Return a pointer to the substring after the last `::` in @p s — i.e. drop
/// any namespace qualification. constexpr so it works inside class-static
/// initialisers (e.g. macro-emitted `wgslName`).
constexpr const char *stripNamespacePrefix(const char *s)
{
	const char *last = s;
	const char *p    = s;
	while (*p)
	{
		if (p[0] == ':' && p[1] == ':')
		{
			last = p + 2;
			p += 2;
		}
		else
		{
			++p;
		}
	}
	return last;
}

} // namespace engine::rendering::shaders

/**
 * @brief Declare a `GpuStructTraits<T>` specialisation by stringifying each
 *        listed C++ field name.
 *
 *   ENGINE_GPU_STRUCT(engine::rendering::FrameUniforms,
 *       viewMatrix,
 *       projectionMatrix,
 *       viewProjectionMatrix,
 *       cameraWorldPosition,
 *       time)
 *
 * Pass the fully-qualified type name; the macro stores the unqualified
 * trailing identifier in `wgslName` (`"FrameUniforms"`, not the full path).
 * Field tokens must match the C++ field names exactly — they're stringified
 * verbatim to produce the WGSL field names.
 *
 * Max 16 fields (matches `aggregate_reflect::kMaxFields`). Expand the macro
 * at global namespace scope; it emits a global-scope template specialisation.
 */
#define ENGINE_GPU_STRUCT(StructType, ...)                                                                              \
	template <> struct ::engine::rendering::shaders::GpuStructTraits<StructType>                                        \
	{                                                                                                                   \
		static constexpr const char *wgslName    =                                                                      \
			::engine::rendering::shaders::stripNamespacePrefix(ENGINE_PP_STRINGIFY(StructType));                        \
		static constexpr const char *fieldNames[] = { ENGINE_PP_FOREACH_STRINGIFY(__VA_ARGS__) };                       \
	}
