#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "engine/rendering/shaders/WgslTypes.h"

namespace engine::rendering::shaders
{

/**
 * @brief One field inside a C++ struct mirrored to GPU.
 *
 * `offsetBytes` MUST equal `offsetof(T, member)` in the matching C++ struct.
 * The descriptor builder asserts this at construction.
 */
struct StructField
{
	std::string name;          ///< WGSL field name (snake_case by convention).
	WgslType    type;
	uint32_t    offsetBytes;
};

/**
 * @brief Runtime description of a C++ struct's GPU layout.
 *
 * Source of truth for: generated WGSL struct text, bind-group buffer size,
 * post-codegen validation, and (eventually) name-keyed material setters.
 * Mismatches between the descriptor and the C++ struct are caught by
 * `build<T>()` at engine startup, not deferred to a shader-compile error.
 */
struct StructDescriptor
{
	std::string              name;        ///< WGSL type name, e.g. "FrameUniforms".
	uint32_t                 sizeBytes;   ///< Equal to sizeof(T) for the C++ struct.
	uint32_t                 alignBytes;  ///< Always rounded up to 16 for UBO use.
	std::vector<StructField> fields;

	/**
	 * @brief Construct + validate a descriptor against a C++ struct type.
	 *
	 * Asserts (in Debug): sizeBytes == sizeof(T), every field's offset is at a
	 * valid std140 alignment, fields appear in increasing offset order, the
	 * final field's end is within sizeof(T), and the struct's size is a
	 * multiple of 16 (UBO requirement).
	 *
	 * Release builds skip the assertions; descriptors are static-initialised
	 * once at startup, so the cost is only paid in Debug.
	 */
	template <typename T>
	[[nodiscard]] static StructDescriptor build(std::string name, std::initializer_list<StructField> fields);
};

/**
 * @brief Engine-known bind-group roles. The codegen and the binder agree on
 *        which integer @group(N) each role maps to (see
 *        `doc/SpecShaderSystem.md` §4).
 */
enum class EngineBindGroup : uint8_t
{
	Frame    = 0,
	Scene    = 1,
	Material = 2,
	Object   = 3,
};

constexpr uint32_t canonicalGroupIndex(EngineBindGroup g) { return static_cast<uint32_t>(g); }

/// First @group index available to user / pass-specific bindings. See §4.
constexpr uint32_t kFirstCustomBindGroupIndex = 4;

// ---- Implementation ----

namespace detail
{
void assertDescriptorMatches(std::size_t cppSize, const StructDescriptor &d);
} // namespace detail

template <typename T>
StructDescriptor StructDescriptor::build(std::string name, std::initializer_list<StructField> fields)
{
	StructDescriptor d;
	d.name       = std::move(name);
	d.sizeBytes  = static_cast<uint32_t>(sizeof(T));
	d.alignBytes = 16;
	d.fields.assign(fields.begin(), fields.end());
	detail::assertDescriptorMatches(sizeof(T), d);
	return d;
}

} // namespace engine::rendering::shaders
