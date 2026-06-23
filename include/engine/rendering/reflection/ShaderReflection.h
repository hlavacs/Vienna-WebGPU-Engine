#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::rendering::reflection
{

/// WGSL scalar / vector / matrix base types. Anything not in this enum is
/// treated as a named user type and looked up in the local struct table.
enum class WgslPrimitive
{
	Unknown,
	F32, I32, U32, Bool,
	Vec2F, Vec3F, Vec4F,
	Vec2I, Vec3I, Vec4I,
	Vec2U, Vec3U, Vec4U,
	Mat2x2F, Mat3x3F, Mat4x4F,
	Mat3x4F, Mat4x3F, Mat2x3F, Mat3x2F, Mat2x4F, Mat4x2F,
};

/// Resolved WGSL type description. For struct types, `primitive == Unknown` and
/// `userTypeName` holds the original identifier - the reflector resolves the
/// struct layout in a second pass once all top-level structs are seen.
struct WgslType
{
	WgslPrimitive primitive = WgslPrimitive::Unknown;
	std::string   userTypeName;     ///< non-empty when primitive == Unknown
	uint32_t      arrayLength = 0;  ///< 0 = not an array, UINT32_MAX = runtime-sized
	bool          isArray      = false;
	bool          isAtomic     = false; ///< atomic<u32> etc. (compute storage)
};

enum class ShaderStage : uint32_t
{
	Vertex   = 1 << 0,
	Fragment = 1 << 1,
	Compute  = 1 << 2,
};

using ShaderStageFlags = uint32_t;

enum class BindingKind
{
	Unknown,
	UniformBuffer,
	StorageBufferRO,
	StorageBufferRW,
	Sampler,
	SamplerComparison,
	Texture,
	StorageTexture,
};

enum class TextureViewDim
{
	Unknown, D1, D2, D2Array, D3, Cube, CubeArray,
};

constexpr uint32_t CANONICAL_GROUP_FRAME    = 0;
constexpr uint32_t CANONICAL_GROUP_SCENE    = 1;
constexpr uint32_t CANONICAL_GROUP_MATERIAL = 2;
constexpr uint32_t CANONICAL_GROUP_OBJECT   = 3;
constexpr uint32_t ENGINE_GROUPS_END        = 4;   // [0..3] reserved for engine roles; custom shaders start at 4. Capped by wgpu-native's hardware max of 8 bind groups per pipeline.

/// Per-field metadata in a struct binding. The reflector populates these so
/// materials can index by NAME instead of needing a mirrored C++ struct.
struct StructField
{
	std::string             name;
	WgslType                type;
	uint32_t                offsetBytes = 0;
	uint32_t                sizeBytes   = 0;
	std::optional<uint32_t> location;          ///< @location(N) on struct fields - present on fragment outputs and vertex IO
};

struct StructLayout
{
	std::string              name;       ///< WGSL struct name (e.g. "PBRProperties")
	uint32_t                 sizeBytes   = 0; ///< total size incl. tail padding (header only when hasRuntimeArray)
	uint32_t                 alignBytes  = 1;
	std::vector<StructField> fields;
	bool                     hasRuntimeArray   = false; ///< trailing runtime-sized array
	uint32_t                 runtimeArrayStride = 0;     ///< element stride of the trailing runtime array, 0 if none
};

struct TextureBinding
{
	TextureViewDim viewDim       = TextureViewDim::D2;
	std::string    sampleType;       ///< "f32", "i32", "u32", "depth" (for texture_depth_*)
	bool           multisampled  = false;
};

struct Binding
{
	uint32_t          bindingIndex = 0;
	std::string       wgslName;
	BindingKind       kind         = BindingKind::Unknown;
	ShaderStageFlags  visibility   = 0;                  ///< filled by the reflector after entry-point scan
	uint64_t          minBindingSize = 0;                ///< 0 = runtime-sized
	StructLayout      structLayout;                      ///< meaningful for buffer bindings
	TextureBinding    texture;                           ///< meaningful for texture / storage-texture
};

struct BindGroupLayout
{
	uint32_t              groupIndex = 0;
	std::vector<Binding>  bindings;
};

struct ShaderReflection
{
	std::string                       path;
	std::vector<BindGroupLayout>      bindGroups;       ///< sorted by groupIndex
	std::vector<StructLayout>         structs;
};

} // namespace engine::rendering::reflection
