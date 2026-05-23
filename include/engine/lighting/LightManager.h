#pragma once

#include "engine/core/Handle.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/LightUniforms.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace engine::rendering
{
class RenderCollector; // forward declaration
}

namespace engine::lighting
{

/**
 * @class LightManager
 * @brief CPU-side light aggregation and organization from scene.
 *
 * Manages all lights in the scene, collecting them from RenderCollector,
 * organizing by type, and providing CPU data for GPU transfer.
 * Converts CPU-side Light objects to GPU-compatible LightStruct format.
 */
class LightManager
{
  public:
	LightManager();
	~LightManager() = default;

	/**
	 * @brief Update lights from render collector.
	 * Extracts all lights from the collector and organizes them for GPU use.
	 *
	 * @param collector RenderCollector containing scene lights with transforms
	 */
	void updateLights(const engine::rendering::RenderCollector &collector);

	/**
	 * @brief Get the light data formatted for GPU.
	 * @return Vector of LightStruct ready for writeBuffer
	 */
	const std::vector<engine::rendering::LightStruct> &getLightData() const { return m_lightStructs; }

	/**
	 * @brief Get current light count.
	 * @return Number of lights in the scene
	 */
	uint32_t getLightCount() const { return static_cast<uint32_t>(m_lightStructs.size()); }

	/**
	 * @brief Get maximum light count (for validation).
	 * @return Max allowed lights (e.g., 512)
	 */
	static constexpr uint32_t getMaxLightCount() { return 512; }

	/**
	 * @brief Get size in bytes for GPU buffer allocation.
	 * @return Total bytes needed for light uniform buffer
	 */
	size_t getBufferSizeBytes() const;

	/**
	 * @brief Clear all lights (used for scene transitions).
	 */
	void clear() { m_lightStructs.clear(); }

  private:
	std::vector<engine::rendering::LightStruct> m_lightStructs;

	/**
	 * @brief Convert CPU Light to GPU LightStruct.
	 * @param light CPU-side light
	 * @param transform World transform of light
	 * @return GPU-compatible LightStruct
	 */
	engine::rendering::LightStruct convertToLightStruct(
		const engine::rendering::Light &light,
		const glm::mat4 &transform
	) const;
};

} // namespace engine::lighting
