#pragma once

#include <cstdint>

namespace engine::rendering
{

class Light;

/**
 * @brief Shadow type enumeration.
 * Determines which shadow mapping technique to use.
 */
enum class ShadowType : uint32_t
{
	Directional2D = 0, // Directional light with 2D shadow map (CSM cascades)
	Spot2D = 1,        // Spot light with single 2D shadow map
	PointCube = 2      // Point light with cube shadow map (6 faces)
};

/**
 * @brief Lightweight shadow request descriptor.
 * 
 * Created by RenderCollector during light extraction, consumed by ShadowPass.
 * Does NOT contain matrices - those are computed by ShadowPass based on camera.
 */
struct ShadowRequest
{
	const Light* light;       // Reference to scene light (non-owning)
	ShadowType type;          // Shadow mapping technique
	uint32_t textureIndex;    // Index into shadow texture array (2D or cube)
	
	ShadowRequest(const Light* l, ShadowType t, uint32_t idx)
		: light(l), type(t), textureIndex(idx) {}
};

} // namespace engine::rendering
