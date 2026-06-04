#pragma once

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

namespace engine::rendering::shaders
{

/**
 * @brief WGSL scalar/vector/matrix types the codegen and validator understand.
 *
 * Restricted to the std140-friendly subset used by engine uniform buffers. New
 * types (mat3, f16, arrays) get added here when a descriptor needs them, not
 * speculatively.
 */
enum class WgslType : uint8_t
{
	F32,
	I32,
	U32,
	Vec2,
	Vec3,
	Vec4,
	Mat4,
};

/// WGSL spelling, exactly as it should appear in generated source.
[[nodiscard]] std::string_view wgslTypeName(WgslType t);

/// Byte size in std140 layout. Note Vec3 is 12 even though its alignment is 16.
[[nodiscard]] uint32_t wgslTypeSize(WgslType t);

/// Std140 alignment. Vec3 aligns to 16 — the standard UBO quirk.
[[nodiscard]] uint32_t wgslTypeAlign(WgslType t);

/**
 * @brief Compile-time C++ → WGSL type mapping.
 *
 * Specialised for the glm + scalar types used by engine structs. The primary
 * template is intentionally undefined — referencing an unsupported C++ type
 * fails to compile with a clear "use of undeclared identifier" pointing at
 * the missing specialisation.
 */
template <typename T> constexpr WgslType wgslTypeOf() = delete;

template <> constexpr WgslType wgslTypeOf<float>()      { return WgslType::F32; }
template <> constexpr WgslType wgslTypeOf<int32_t>()    { return WgslType::I32; }
template <> constexpr WgslType wgslTypeOf<uint32_t>()   { return WgslType::U32; }
template <> constexpr WgslType wgslTypeOf<glm::vec2>()  { return WgslType::Vec2; }
template <> constexpr WgslType wgslTypeOf<glm::vec3>()  { return WgslType::Vec3; }
template <> constexpr WgslType wgslTypeOf<glm::vec4>()  { return WgslType::Vec4; }
template <> constexpr WgslType wgslTypeOf<glm::mat4>()  { return WgslType::Mat4; }

} // namespace engine::rendering::shaders
