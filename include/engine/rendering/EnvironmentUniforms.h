#pragma once

#include <glm/glm.hpp>

namespace engine::rendering
{

/**
 * @brief Per-camera environment parameters consumed by PBR forward, deferred
 *        composition, and the skybox pass.
 *
 * Single packed vec4 so the WGSL bind-group entry is a 16-byte UBO:
 *   x = irradiance enabled (0/1 — IBL on/off)
 *   y = irradiance intensity scalar
 *   z = skybox enabled (0/1 — fill background or leave the clear colour)
 *   w = reserved (currently unused)
 *
 * Renderer::updateSceneBindGroup writes this directly to a cached per-camera
 * UBO referenced by the Scene bind group at @binding(5).
 */
struct EnvironmentUniforms
{
	glm::vec4 params{0.0f, 1.0f, 0.0f, 0.0f};
};
static_assert(sizeof(EnvironmentUniforms) % 16 == 0, "EnvironmentUniforms must be 16-byte aligned");

} // namespace engine::rendering
