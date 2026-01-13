#pragma once


#include <glm/glm.hpp>
#include <memory>
#include <optional>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/Texture.h"

namespace engine::rendering
{
struct RenderTarget
{
	RenderTarget() = default;
	RenderTarget(
		uint64_t cameraId,
		std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> gpuTexture,
		glm::vec4 viewport = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
		ClearFlags clearFlags = ClearFlags::SolidColor,
		glm::vec4 backgroundColor = glm::vec4(0.0f),
		std::optional<Texture::Handle> cpuTarget = std::nullopt
	) :
		m_cameraId(cameraId),
		gpuTexture(std::move(gpuTexture)),
		viewport(viewport),
		clearFlags(clearFlags),
		backgroundColor(backgroundColor),
		cpuTarget(std::move(cpuTarget))
	{
	}

	std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> gpuTexture; // actual GPU render target
	glm::vec4 viewport;													  // The relative viewport (x, y, width, height) in [0,1]
	ClearFlags clearFlags;
	glm::vec4 backgroundColor;
	std::optional<Texture::Handle> cpuTarget; // optional CPU-side texture
	uint64_t m_cameraId;					// associated camera ID
};
} // namespace engine::rendering