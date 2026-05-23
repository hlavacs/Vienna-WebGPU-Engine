#include "engine/lighting/LightManager.h"

#include "engine/rendering/RenderCollector.h"
#include <spdlog/spdlog.h>

namespace engine::lighting
{

LightManager::LightManager() = default;

void LightManager::updateLights(const engine::rendering::RenderCollector &collector)
{
	m_lightStructs.clear();

	const auto &lights = collector.getLights();

	if (lights.size() > getMaxLightCount())
	{
		spdlog::warn("Light count {} exceeds maximum {}, truncating", lights.size(), getMaxLightCount());
	}

	// Reserve capacity
	m_lightStructs.reserve(std::min(lights.size(), static_cast<size_t>(getMaxLightCount())));

	// Convert each light to GPU-compatible struct
	for (size_t i = 0; i < lights.size() && i < getMaxLightCount(); ++i)
	{
		const auto &light = lights[i];
		m_lightStructs.push_back(convertToLightStruct(light, light.getTransform()));
	}

	spdlog::debug("LightManager updated with {} lights", m_lightStructs.size());
}

engine::rendering::LightStruct LightManager::convertToLightStruct(
	const engine::rendering::Light &light,
	const glm::mat4 &transform
) const
{
	// Use Light's built-in conversion with proper transform
	auto lightStruct = light.toUniforms();
	lightStruct.transform = transform;
	// shadowIndex and shadowCount will be set by ShadowPass for assigned lights
	return lightStruct;
}

size_t LightManager::getBufferSizeBytes() const
{
	// Header (4 bytes for count + 12 bytes padding) + light structs
	constexpr size_t HEADER_SIZE = 16; // Alignment requirement
	return HEADER_SIZE + (m_lightStructs.size() * sizeof(engine::rendering::LightStruct));
}

} // namespace engine::lighting
