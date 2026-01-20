#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <vector>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/Submesh.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

// Forward declarations
namespace engine::rendering::webgpu
{
class WebGPUModel;
class WebGPUMesh;
class WebGPUMaterial;
class WebGPUBindGroup;
class WebGPUContext;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

// Forward declarations
class RenderCollector;

/**
 * @brief GPU-side render item prepared for actual rendering.
 * Contains GPU resources created once and reused across multiple passes.
 */
struct RenderItemGPU
{
	std::shared_ptr<webgpu::WebGPUModel> gpuModel;
	webgpu::WebGPUMesh *gpuMesh;
	std::shared_ptr<webgpu::WebGPUMaterial> gpuMaterial;
	std::shared_ptr<webgpu::WebGPUBindGroup> objectBindGroup;
	engine::rendering::Submesh submesh;
	glm::mat4 worldTransform;
	uint32_t renderLayer;
	uint64_t objectID;
};

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
 * 
 * Centralizes all GPU-ready data used by rendering passes:
 * - CPU-side Light objects
 * - GPU-ready LightStruct and ShadowUniform arrays
 * - Prepared GPU render items (models, meshes, materials)
 * - Render targets for all cameras this frame
 */
struct FrameCache
{
	std::vector<Light> lights;
	std::vector<LightStruct> lightUniforms;
	std::vector<ShadowUniform> shadowUniforms;
	std::vector<RenderTarget> renderTargets;
	std::vector<std::optional<RenderItemGPU>> gpuRenderItems; // Lazy-prepared GPU resources
	float time = 0.0f;

	/**
	 * @brief Prepares GPU resources for the specified indices from the collector.
	 * @param context WebGPU context for resource creation.
	 * @param collector The render collector with CPU-side data.
	 * @param indices Indices of items to prepare.
	 * @return Reference to gpuRenderItems for rendering.
	 */
	std::vector<std::optional<RenderItemGPU>>& prepareGPUResources(
		std::shared_ptr<webgpu::WebGPUContext> context,
		const RenderCollector &collector,
		const std::vector<size_t> &indices
	);

	/**
	 * @brief Clears all frame cache data.
	 * Call at the end of each frame to reset for the next frame.
	 */
	void clear()
	{
		lights.clear();
		lightUniforms.clear();
		shadowUniforms.clear();
		renderTargets.clear();
		gpuRenderItems.clear();
		time = 0.0f;
	}
};

} // namespace engine::rendering