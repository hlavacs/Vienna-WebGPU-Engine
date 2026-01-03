#pragma once

#include "engine/core/Enum.h"

namespace engine::rendering
{
ENUM_BIT_FLAGS_WRAPPED(
	ShaderFeature,
	Flag,
	5,
	None = 0,
	UsesNormalMap = 1 << 0,
	AlphaTest = 1 << 1,
	Skinned = 1 << 2,
	Instanced = 1 << 3
)

}