#pragma once

#include "engine/core/Enum.h"
#include <cstdint>

namespace engine::rendering
{

ENUM_BIT_FLAGS64_WRAPPED(
	MaterialFeature,
	Flag,
	15,

	None = 0,

	// --- Texture presence ---
	UsesBaseColorMap = 1 << 0,
	UsesNormalMap = 1 << 1,
	UsesOcclusionMap = 1 << 2,
	UsesEmissiveMap = 1 << 3,

	// --- PBR workflows ---
	UsesMetallicRoughnessMap = 1 << 4, // packed ORM (glTF style)
	UsesMetallicMap = 1 << 5,		   // separate metallic
	UsesRoughnessMap = 1 << 6,		   // separate roughness
	UsesSpecularGlossiness = 1 << 7,   // alternative PBR workflow

	// --- Extra maps ---
	UsesHeightMap = 1 << 8, // Parallax / Displacement

	// --- Render behavior ---
	AlphaTest = 1 << 9,
	Transparent = 1 << 10,
	DoubleSided = 1 << 11,

	// --- Mesh features ---
	Skinned = 1 << 12,
	Instanced = 1 << 13
)

} // namespace engine::rendering
