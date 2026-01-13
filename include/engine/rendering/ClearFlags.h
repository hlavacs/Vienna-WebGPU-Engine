#pragma once
#include "engine/core/Enum.h"

namespace engine::rendering
{
/**
 * @brief Clear flags enumeration.
 */
enum class ClearFlags : uint32_t
{
	None = 0,			 ///< Do not clear any buffer (UI Overlay).
	SolidColor = 1 << 0, ///< Clear to a solid background color.
	Skybox = 1 << 1,	 ///< Draw the scene's skybox as background.
	Depth = 1 << 2,		 ///< Clear depth buffer.
};

ENUM_BIT_OPERATORS(ClearFlags)
ENUM_BIT_FLAGS_HAS(ClearFlags)

} // namespace engine::rendering