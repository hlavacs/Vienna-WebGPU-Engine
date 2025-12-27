#pragma once

namespace engine::rendering
{
/**
 * @brief Clear flags enumeration.
 */
enum class ClearFlags
{
	SolidColor, ///< Clear to a solid background color.
	Skybox,		///< Draw the scene's skybox as background.
	DepthOnly,	///< Only clear depth buffer (useful for overlay cameras).
	Nothing		///< Do not clear any buffer (rarely used).
};
} // namespace engine::rendering