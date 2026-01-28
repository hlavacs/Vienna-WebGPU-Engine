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
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ShadowRequest.h"
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
class RenderCollector;
struct RenderTarget;

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
class ShadowPass : public RenderPass
{
  public:
	std::shared_ptr<webgpu::WebGPUTexture> DEBUG_SHADOW_CUBE_ARRAY; //< Debug copy of cube shadow maps
	std::shared_ptr<webgpu::WebGPUTexture> DEBUG_SHADOW_2D_ARRAY;	//< Debug copy of 2D shadow maps

	/**
	 * @brief Construct a shadow pass.
	 * @param context The WebGPU context.
	 */
	explicit ShadowPass(std::shared_ptr<webgpu::WebGPUContext> context);

	~ShadowPass() override = default;

	/**
	 * @brief Initialize shadow pass resources (bind group layouts, uniforms).
	 * @return True if initialization succeeded.
	 */
	bool initialize() override;

	/**
	 * @brief Render all shadow maps from shadow requests (camera-aware).
	 *
	 * This method is camera-aware and computes shadow matrices per-camera:
	 * - For directional lights: Computes CSM cascades based on camera frustum
	 * - For spot lights: Computes perspective shadow matrix
	 * - For point lights: Computes 6 cube face matrices
	 *
	 * Shadow matrices are computed here, not stored in Light objects.
	 * Results are written to frameCache.shadowUniforms for GPU upload.
	 *
	 * @param frameCache Frame-wide data storage (reads shadowRequests, writes shadowUniforms, gpuRenderItems)
	 */
	void render(FrameCache &frameCache) override;

	/**
	 * @brief Clean up GPU resources.
	 */
	void cleanup() override;

	/**
	 * @brief Set the render collector containing scene data.
	 * @param collector The render collector with render items and lights.
	 */
	void setRenderCollector(const RenderCollector *collector) { m_collector = collector; }

	/**
	 * @brief Set the camera ID for bind group caching.
	 * @param id Camera identifier.
	 */
	void setCameraId(uint64_t id)
	{
		m_cameraId = id;
	}

	/**
	 * @brief Get the shadow bind group for use in mesh rendering.
	 * @return Shared pointer to shadow bind group (contains shadow textures and sampler).
	 */
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUBindGroup> getShadowBindGroup() const { return m_shadowBindGroup; }

	/**
	 * @brief Check if debug mode is enabled.
	 * In debug mode, shadow maps may be copied for visualization.
	 * @return True if debug mode is enabled.
	 */
	bool isDebugMode() const { return m_isDebugMode; }

	/**
	 * @brief Enable or disable debug mode.
	 * In debug mode, shadow maps may be copied for visualization.
	 */
	void setDebugMode(bool debugMode) { m_isDebugMode = debugMode; }

  private:
	/**
	 * @brief Render shadow map for a single directional or spot light (2D shadow map).
	 *
	 * @param gpuItems Filtered render items visible from light (caller performs culling).
	 * @param indicesToRender Indices of items to render (subset of gpuItems).
	 * @param shadowTexture Target depth texture (owned by caller).
	 * @param arrayLayer Array layer index to render into.
	 * @param lightViewProjection Light's view-projection matrix (calculated by caller).
	 * @param lightPosition Position of the light in world space.
	 * @param farPlane Far plane distance for the light's projection matrix.
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
		const glm::mat4 &lightViewProjection,
		const glm::vec3 &lightPosition,
		float farPlane
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
	 * @brief Compute shadow matrix and create ShadowUniform from a ShadowRequest.
	 * This is where per-camera shadow matrices are computed.
	 * @param request Shadow request from RenderCollector
	 * @param renderTarget Current render target (provides camera info)
	 * @param splitLambda CSM split lambda (0=uniform, 1=logarithmic)
	 * @return ShadowUniform with computed view-projection matrix
	 */
	std::vector<ShadowUniform> ShadowPass::computeShadowUniforms(
		const ShadowRequest &request,
		const engine::rendering::RenderTarget &renderTarget,
		float splitLambda = 0.5f
	);

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

	// External dependencies (set via setters)
	const RenderCollector *m_collector = nullptr;

	size_t m_cameraId = 0; //< Current camera ID for multi-camera setups
	bool m_isDebugMode = false;

	// Shadow map resources
	std::shared_ptr<webgpu::WebGPUTexture> m_shadow2DArray;
	std::shared_ptr<webgpu::WebGPUTexture> m_shadowCubeArray;
	std::shared_ptr<webgpu::WebGPUBuffer> m_shadowUniforms; // Unified buffer for all shadow types
	wgpu::Sampler m_shadowSampler = nullptr;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowBindGroup;

	// Bind group layouts (from shaders)
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_shadowPass2DBindGroupLayout;	//< For 2D shadows
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_shadowPassCubeBindGroupLayout; //< For cube shadows

	// Uniform buffers (reusable across lights)
	std::shared_ptr<webgpu::WebGPUBuffer> m_shadowPass2DUniformsBuffer;	  //< 2D shadow uniforms
	std::shared_ptr<webgpu::WebGPUBuffer> m_shadowPassCubeUniformsBuffer; //< Cube shadow uniforms

	// Reusable bind groups (updated per-light via writeBuffer)
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowPass2DBindGroup;	   //< For 2D shadows
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowPassCubeBindGroup[6]; //< For cube shadows

	// Pipeline caching (by topology type, NOT per-mesh instance)
	// Using weak_ptr: if pipeline is released elsewhere, we'll recreate it
	std::unordered_map<int, std::weak_ptr<webgpu::WebGPUPipeline>> m_pipelineCache;		//< 2D shadow pipelines
	std::unordered_map<int, std::weak_ptr<webgpu::WebGPUPipeline>> m_cubePipelineCache; //< Cube shadow pipelines
};

} // namespace engine::rendering
