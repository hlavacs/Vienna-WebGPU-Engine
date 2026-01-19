#pragma once

#include "engine/core/Enum.h"

namespace engine::rendering
{
ENUM_BIT_FLAGS_WRAPPED(
	ShaderFeature,
	Flag,
	5,
	None = 0,
	AlphaTest = 1 << 0,
	Skinned = 1 << 1,
	Instanced = 1 << 2
)

}