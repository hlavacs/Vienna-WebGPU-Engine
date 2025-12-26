#pragma once

#include <cstdint>

namespace engine::rendering
{
enum class MaterialFeature : uint32_t
{
	None = 0,						// Default, no special features
	UsesNormalMap = 1 << 0,			// Material has a normal map
	AlphaTest = 1 << 1,				// Material requires alpha cutoff
	Skinned = 1 << 2,				// Material is on a skinned mesh
	Instanced = 1 << 3,				// Material is drawn with instances
	UsesMetallicRoughness = 1 << 4, // PBR metallic-roughness workflow
	UsesOcclusionMap = 1 << 5,		// Material has occlusion map
	UsesEmissiveMap = 1 << 6,		// Material has emissive map
	Transparent = 1 << 7,			// Material is semi-transparent
	DoubleSided = 1 << 8,			// Material disables backface culling
	// Extend with additional material-driven pipeline features
};

// Bitwise operators for enum class (modern C++ best practice)
inline constexpr MaterialFeature operator|(MaterialFeature a, MaterialFeature b) noexcept
{
	return static_cast<MaterialFeature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr MaterialFeature operator&(MaterialFeature a, MaterialFeature b) noexcept
{
	return static_cast<MaterialFeature>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr MaterialFeature operator~(MaterialFeature a) noexcept
{
	return static_cast<MaterialFeature>(~static_cast<uint32_t>(a));
}

} // namespace engine::rendering
