#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <vector>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering
{

/**
 * @brief Per-camera render target information.
 * Contains all data needed to render from a specific camera's perspective.
 * Decouples the renderer from scene graph nodes.
 */
struct RenderTarget
{
	uint64_t cameraId;
	glm::mat4 viewMatrix;
	glm::mat4 projectionMatrix;
	glm::mat4 viewProjectionMatrix;
	glm::vec3 cameraPosition;
	glm::vec4 viewport; // (x, y, width, height) in normalized coordinates
	ClearFlags clearFlags;
	glm::vec4 backgroundColor;
	std::optional<Texture::Handle> cpuTarget;
	std::shared_ptr<webgpu::WebGPUTexture> gpuTexture; // actual GPU render target texture

	/**
	 * @brief Constructs FrameUniforms from this RenderTarget.
	 * @param time Current frame time in seconds.
	 * @return FrameUniforms ready for GPU upload.
	 */
	FrameUniforms getFrameUniforms(float time) const
	{
		FrameUniforms uniforms{};
		uniforms.viewMatrix = viewMatrix;
		uniforms.projectionMatrix = projectionMatrix;
		uniforms.viewProjectionMatrix = viewProjectionMatrix;
		uniforms.cameraWorldPosition = cameraPosition;
		uniforms.time = time;
		return uniforms;
	}
};

/**
 * @brief Frame cache for rendering-wide data.
 * Contains data that applies to the entire current frame and should be
 * cleared/reset at the start and end of each frame.
 */
struct FrameCache
{
	std::vector<Light> lights;
	std::vector<RenderTarget> renderTargets;
	float time = 0.0f;

	/**
	 * @brief Clears all frame cache data.
	 * Call at the end of each frame to reset for the next frame.
	 */
	void clear()
	{
		lights.clear();
		renderTargets.clear();
		time = 0.0f;
	}
};

} // namespace engine::rendering