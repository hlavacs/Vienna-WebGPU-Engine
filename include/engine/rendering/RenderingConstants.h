#pragma once

#include <cstdint>

/**
 * @brief Global rendering configuration constants.
 *
 * TODO: These should be moved to a proper engine configuration system
 * that allows runtime configuration and validation.
 */
namespace engine::rendering::constants
{
// Shadow mapping configuration
constexpr uint32_t MAX_SHADOW_MAPS_2D = 16;	 // Max directional/spot light shadows
constexpr uint32_t MAX_SHADOW_MAPS_CUBE = 4; // Max point light shadows
constexpr uint32_t DEFAULT_SHADOW_MAP_SIZE = 2048;
constexpr uint32_t DEFAULT_CUBE_SHADOW_MAP_SIZE = 1024;

// Light configuration
constexpr uint32_t MAX_LIGHTS = 16;

} // namespace engine::rendering::constants
