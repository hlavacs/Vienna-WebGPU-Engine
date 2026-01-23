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
	Spot2D = 1,		   // Spot light with single 2D shadow map
	PointCube = 2	   // Point light with cube shadow map (6 faces)
};

/**
 * @brief Lightweight shadow request descriptor.
 *
 * Created by RenderCollector during light extraction, consumed by ShadowPass.
 * Does NOT contain matrices - those are computed by ShadowPass based on camera.
 */
struct ShadowRequest
{
	const Light *light;			// Reference to scene light (non-owning)
	ShadowType type;			// Shadow mapping technique
	uint32_t textureIndexStart; // Starting index into shadow texture array
	uint32_t cascadeCount;		// Number of cascades (1 for non-CSM, 2-4 for CSM)

	ShadowRequest(const Light *l, ShadowType t, uint32_t idxStart, uint32_t cascades = 1) : light(l), type(t), textureIndexStart(idxStart), cascadeCount(cascades) {}
};

} // namespace engine::rendering
