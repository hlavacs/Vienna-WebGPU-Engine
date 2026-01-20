#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/ShadowUniforms.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering
{
struct RenderItemGPU;
struct FrameCache;
struct ShadowResources;
class RenderCollector;

namespace webgpu
{
class WebGPUContext;
}

/**
 * @class ShadowPass
 * @brief Single-light shadow rendering pass.
 *
 * RESPONSIBILITIES:
 * - Creates pipelines for shadow rendering
 * - Manages bind groups and uniform buffers
 * - Records render commands into provided depth textures
 *
 * DOES NOT:
 * - Iterate over multiple lights (single-light per call)
 * - Own or allocate shadow map textures (textures provided by caller)
 * - Perform light-specific culling (caller provides filtered items)
 * - Cache per-light resources (pipelines cached by mesh properties only)
 *
 * Designed for use in render graphs and flexible rendering pipelines.
 */
class ShadowPass
{
  public:
	/**
	 * @brief Construct a shadow pass.
	 * @param context The WebGPU context.
	 */
	explicit ShadowPass(std::shared_ptr<webgpu::WebGPUContext> context);

	~ShadowPass() = default;

	/**
	 * @brief Initialize shadow pass resources (bind group layouts, uniforms).
	 * @return True if initialization succeeded.
	 */
	bool initialize();

	/**
	 * @brief Set the render collector containing scene data.
	 * @param collector The render collector with render items and lights.
	 */
	void setRenderCollector(const RenderCollector* collector) { m_collector = collector; }

	/**
	 * @brief Set shadow resources (textures, bind groups).
	 * @param resources Shadow textures and bind groups.
	 */
	void setShadowResources(const ShadowResources* resources) { m_shadowResources = resources; }

	/**
	 * @brief Set the WebGPU context.
	 * @param context WebGPU context for resource creation.
	 */
	void setContext(std::shared_ptr<webgpu::WebGPUContext> context) { m_context = context; }

	/**
	 * @brief Render all shadow maps for all lights.
	 * Orchestrates the entire shadow rendering process:
	 * - Extracts lights and shadow uniforms from collector
	 * - Performs culling for each light
	 * - Renders shadow maps (2D or cube)
	 * - Updates frameCache with light and shadow uniforms
	 * 
	 * @param frameCache Frame-wide data storage (reads/writes lights, shadows, gpuRenderItems)
	 */
	void render(FrameCache &frameCache);

	/**
	 * @brief Render shadow map for a single directional or spot light (2D shadow map).
	 *
	 * @param gpuItems Filtered render items visible from light (caller performs culling).
	 * @param indicesToRender Indices of items to render (subset of gpuItems).
	 * @param shadowTexture Target depth texture (owned by caller).
	 * @param arrayLayer Array layer index to render into.
	 * @param lightViewProjection Light's view-projection matrix (calculated by caller).
	 *
	 * Caller responsibilities:
	 * - Filter items to only those visible from light
	 * - Provide correct texture array and layer index
	 * - Calculate light view-projection matrix
	 */
	void renderShadow2D(
		const std::vector<std::optional<RenderItemGPU>> &gpuItems,
		const std::vector<size_t> &indicesToRender, // only items visible to this light
		const std::shared_ptr<webgpu::WebGPUTexture> shadowTexture,
		uint32_t arrayLayer,
		const glm::mat4 &lightViewProjection
	);

	/**
	 * @brief Render shadow map for a single point light (cube shadow map).
	 *
	 * @param items Filtered render items visible from light (caller performs culling).
	 * @param indicesToRender Indices of items to render (subset of items).
	 * @param shadowTexture Target cube depth texture array (owned by caller).
	 * @param cubeIndex Cube array index (6 consecutive layers per cube).
	 * @param lightPosition Position of the point light in world space.
	 * @param farPlane Far plane distance for the light's projection matrix.
	 *
	 * Caller responsibilities:
	 * - Filter items to only those within light range
	 * - Provide correct cube texture array and index
	 * - Ensure texture has 6 consecutive layers per cube (cubeIndex * 6 + faceIndex)
	 */
	void renderShadowCube(
		const std::vector<std::optional<RenderItemGPU>> &items,
		const std::vector<size_t> &indicesToRender,
		const std::shared_ptr<webgpu::WebGPUTexture> shadowTexture,
		uint32_t cubeIndex,
		const glm::vec3 &lightPosition,
		float farPlane
	);

	/**
	 * @brief Clear cached pipelines.
	 * Call this when shaders are reloaded or graphics state changes.
	 */
	void cleanup();

  private:
	/**
	 * @brief Get or create shadow pipeline for a given topology and shader type.
	 * Pipelines are cached by topology type, NOT per-mesh instance.
	 */
	std::shared_ptr<webgpu::WebGPUPipeline> getOrCreatePipeline(
		engine::rendering::Topology::Type topology,
		bool isCubeShadow
	);

	/**
	 * @brief Render items into the current render pass.
	 * Handles pipeline binding, mesh binding, and draw calls.
	 * @param renderPass The active render pass encoder.
	 * @param gpuItems Filtered render items visible from light (caller performs culling).
	 * @param indicesToRender Indices of items to render (subset of gpuItems).
	 * @param isCubeShadow True if rendering to cube shadow map, false for 2D shadow.
	 */
	void renderItems(
		wgpu::RenderPassEncoder &renderPass,
		const std::vector<std::optional<RenderItemGPU>> &gpuItems,
		const std::vector<size_t> &indicesToRender,
		bool isCubeShadow
	);

	std::shared_ptr<webgpu::WebGPUContext> m_context;

	// External dependencies (set via setters)
	const RenderCollector* m_collector = nullptr;
	const ShadowResources* m_shadowResources = nullptr;

	// Bind group layouts (from shaders)
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_shadowBindGroupLayout;		//< For 2D shadows
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_shadowCubeBindGroupLayout; //< For cube shadows

	// Uniform buffers (reusable across lights)
	std::shared_ptr<webgpu::WebGPUBuffer> m_shadowUniformsBuffer;	  //< 2D shadow uniforms
	std::shared_ptr<webgpu::WebGPUBuffer> m_shadowCubeUniformsBuffer; //< Cube shadow uniforms

	// Reusable bind groups (updated per-light via writeBuffer)
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowBindGroup;		//< For 2D shadows
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowCubeBindGroup; //< For cube shadows

	// Pipeline caching (by topology type, NOT per-mesh instance)
	// Using weak_ptr: if pipeline is released elsewhere, we'll recreate it
	std::unordered_map<int, std::weak_ptr<webgpu::WebGPUPipeline>> m_pipelineCache;	   //< 2D shadow pipelines
	std::unordered_map<int, std::weak_ptr<webgpu::WebGPUPipeline>> m_cubePipelineCache; //< Cube shadow pipelines
};

} // namespace engine::rendering
