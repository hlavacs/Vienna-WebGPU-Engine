#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Mesh.h"
#include "engine/rendering/RenderPass.h"

namespace engine::rendering
{

struct FrameCache;
struct RenderTarget;
struct ShadowRequest;
struct ShadowUniform;
class RenderCollector;

namespace webgpu
{
class WebGPUBindGroup;
class WebGPUBindGroupLayoutInfo;
class WebGPUContext;
class WebGPUPipeline;
class WebGPUTexture;
} // namespace webgpu

/**
 * @brief Renders shadow maps for directional, spot, and point lights.
 *
 * Computes shadow matrices per camera (CSM cascades, perspective projections, cube face matrices)
 * and renders depth passes into shadow map texture arrays.
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
 *
 * Usage:
 * @code
 *   shadowPass.setRenderCollector(&collector);
 *   shadowPass.setCameraId(cameraId);
 *   shadowPass.render(frameCache);
 * @endcode
 */
class ShadowPass : public RenderPass
{
  public:
	std::shared_ptr<webgpu::WebGPUTexture> DEBUG_SHADOW_CUBE_ARRAY; ///< Debug visualization of cube shadow maps
	std::shared_ptr<webgpu::WebGPUTexture> DEBUG_SHADOW_2D_ARRAY;	///< Debug visualization of 2D shadow maps

	/**
	 * @brief Construct a shadow pass.
	 * @param context WebGPU rendering context
	 */
	explicit ShadowPass(std::shared_ptr<webgpu::WebGPUContext> context);

	~ShadowPass() override = default;

	/**
	 * @brief Initialize shadow pass resources (shaders, bind groups, textures).
	 * @return True if initialization succeeded
	 */
	bool initialize() override;

	/**
	 * @brief Render all shadow maps from frameCache.shadowRequests.
	 *
	 * Computes shadow matrices based on camera frustum and light properties,
	 * culls scene geometry per light, and renders depth passes.
	 *
	 * @param frameCache Frame data (reads shadowRequests, writes shadowUniforms)
	 */
	void render(FrameCache &frameCache) override;

	/**
	 * @brief Clean up GPU resources.
	 */
	void cleanup() override;

	/**
	 * @brief Set the render collector containing scene geometry and lights.
	 * @param collector Scene data provider
	 */
	void setRenderCollector(const RenderCollector *collector) { m_collector = collector; }

	/**
	 * @brief Set the camera ID for shadow matrix computation.
	 * @param id Camera identifier
	 */
	void setCameraId(uint64_t id) { m_cameraId = id; }

	/**
	 * @brief Enable or disable debug mode (visualizes shadow maps as color).
	 * @param debugMode True to enable debug visualization
	 */
	void setDebugMode(bool debugMode) { m_isDebugMode = debugMode; }

	/**
	 * @brief Get the shadow bind group for use in material shaders.
	 * @return Shadow bind group containing sampler and shadow map textures
	 */
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUBindGroup> getShadowBindGroup() const { return m_shadowBindGroup; }

	/**
	 * @brief Check if debug mode is enabled.
	 * @return True if debug visualization is active
	 */
	[[nodiscard]] bool isDebugMode() const { return m_isDebugMode; }

  private:
	/**
	 * @brief Render a 2D shadow map (directional or spot light).
	 * @param frameCache Frame data containing GPU render items
	 * @param indicesToRender Indices of visible items to render
	 * @param arrayLayer Target texture array layer
	 * @param shadowUniform Shadow parameters (view-projection matrix, bias, etc.)
	 */
	void renderShadow2D(
		FrameCache &frameCache,
		const std::vector<size_t> &indicesToRender,
		uint32_t arrayLayer,
		const ShadowUniform &shadowUniform
	);

	/**
	 * @brief Render a cube shadow map (point light, 6 faces).
	 * @param frameCache Frame data containing GPU render items
	 * @param indicesToRender Indices of visible items to render
	 * @param cubeIndex Target cube array index (6 layers per cube)
	 * @param shadowUniform Shadow parameters (light position, range, bias, etc.)
	 */
	void renderShadowCube(
		FrameCache &frameCache,
		const std::vector<size_t> &indicesToRender,
		uint32_t cubeIndex,
		const ShadowUniform &shadowUniform
	);

	/**
	 * @brief Compute shadow uniforms from a shadow request.
	 *
	 * Generates view-projection matrices based on light type:
	 * - Directional: Orthographic projection(s), optionally cascaded
	 * - Spot: Perspective projection
	 * - Point: Stored light position (cube faces computed at render time)
	 *
	 * @param request Shadow request from render collector
	 * @param renderTarget Camera render target (provides frustum for CSM)
	 * @param splitLambda CSM split weight (0=uniform, 1=logarithmic)
	 * @return Vector of shadow uniforms (1 for spot/point, N for cascaded directional)
	 */
	std::vector<ShadowUniform> computeShadowUniforms(
		const ShadowRequest &request,
		const RenderTarget &renderTarget,
		float splitLambda = 0.5f
	);

	/**
	 * @brief Get or create a shadow pipeline for a mesh topology.
	 *
	 * Pipelines are cached by topology and shadow type (2D vs cube).
	 *
	 * @param topology Mesh topology (triangles, lines, etc.)
	 * @param isCubeShadow True for cube shadow shader, false for 2D
	 * @return Shadow pipeline, or nullptr on failure
	 */
	std::shared_ptr<webgpu::WebGPUPipeline> getOrCreatePipeline(
		Topology::Type topology,
		bool isCubeShadow
	);

	/**
	 * @brief Render geometry items into the active shadow pass.
	 * @param renderPass Active render pass encoder
	 * @param frameCache Frame data for bind group lookup
	 * @param indicesToRender Indices of items to render
	 * @param isCubeShadow True if rendering to cube shadow map
	 * @param faceIndex Cube face index (0-5), ignored for 2D shadows
	 */
	void renderItems(
		wgpu::RenderPassEncoder &renderPass,
		FrameCache &frameCache,
		const std::vector<size_t> &indicesToRender,
		bool isCubeShadow,
		uint32_t faceIndex = 0
	);

	const RenderCollector *m_collector = nullptr; ///< Scene geometry and light provider
	size_t m_cameraId = 0;						  ///< Active camera for shadow matrix computation
	bool m_isDebugMode = false;					  ///< Enable debug visualization

	std::shared_ptr<webgpu::WebGPUTexture> m_shadow2DArray;		///< 2D shadow map texture array
	std::shared_ptr<webgpu::WebGPUTexture> m_shadowCubeArray;	///< Cube shadow map texture array
	wgpu::Sampler m_shadowSampler = nullptr;					///< Shadow comparison sampler
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowBindGroup; ///< Shadow maps bind group for material shaders

	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_shadowPass2DBindGroupLayout;	///< 2D shadow pass layout
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_shadowPassCubeBindGroupLayout; ///< Cube shadow pass layout

	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowPass2DBindGroup;	   ///< Reusable 2D shadow pass bind group
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowPassCubeBindGroup[6]; ///< Reusable cube face bind groups

	std::unordered_map<int, std::weak_ptr<webgpu::WebGPUPipeline>> m_pipelineCache;		///< 2D shadow pipeline cache
	std::unordered_map<int, std::weak_ptr<webgpu::WebGPUPipeline>> m_cubePipelineCache; ///< Cube shadow pipeline cache
};

} // namespace engine::rendering