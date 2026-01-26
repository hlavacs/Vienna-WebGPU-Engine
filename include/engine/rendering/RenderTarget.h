#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <optional>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

// Forward declarations
namespace engine::rendering::webgpu
{
class WebGPUTexture;
} // namespace engine::rendering::webgpu

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
	int layerIndex{-1};								   // for texture arrays or cube maps

	/**
	 * @brief Constructs FrameUniforms from this RenderTarget.
	 * @param time Current frame time in seconds.
	 * @return FrameUniforms ready for GPU upload.
	 */
	[[nodiscard]] FrameUniforms getFrameUniforms(float time) const
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

} // namespace engine::rendering