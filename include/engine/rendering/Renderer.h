#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/CompositePass.h"
#include "engine/rendering/DebugCollector.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/MeshPass.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderPassManager.h"
#include "engine/rendering/RenderTarget.h"
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/ShadowPass.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine
{
class GameEngine; // forward declaration
} // namespace engine

namespace engine::rendering
{

/**
 * @brief Central renderer that orchestrates the rendering pipeline.
 *
 * Manages render passes, pipelines, and executes rendering of collected
 * scene data. Separates rendering logic from application/scene logic.
 */
class Renderer
{
  public:
	friend class engine::GameEngine;

	Renderer(std::shared_ptr<webgpu::WebGPUContext> context);
	~Renderer();

	/**
	 * @brief Initializes the renderer with default passes and pipelines.
	 * @return True if initialization succeeded.
	 */
	bool initialize();

	/**
	 * @brief Main public render frame method. Orchestrates the entire rendering pipeline.
	 * This is the only public method that should be called from GameEngine.
	 * Completely decoupled from scene nodes - uses extracted RenderTarget and FrameCache instead.
	 * @param frameCache Frame-wide data (lights, time, render targets).
	 * @param renderCollector Pre-collected render data from the scene.
	 * @param time Current time in seconds.
	 * @param uiCallback Optional callback for rendering UI on top of the scene.
	 * @return True if frame rendered successfully.
	 */
	bool renderFrame(
		std::vector<RenderTarget> &renderTargets,
		const RenderCollector &renderCollector,
		float time,
		std::function<void(wgpu::RenderPassEncoder)> uiCallback = nullptr
	);

	/**
	 * @brief Gets the WebGPU context.
	 * @return Pointer to WebGPU context.
	 */
	[[nodiscard]] webgpu::WebGPUContext *getWebGPUContext() const { return m_context.get(); }

	/**
	 * @brief Handles window resize events.
	 * @param width New width.
	 * @param height New height.
	 */
	void onResize(uint32_t width, uint32_t height);

  private:
	// ========================================
	// Frame Orchestration (High-Level Flow)
	// ========================================

	/**
	 * @brief Acquires surface texture and clears frame cache.
	 * Called at the start of each frame.
	 */
	void startFrame();

	/**
	 * @brief Renders camera view to a texture.
	 * Performs frustum culling, prepares GPU resources, delegates to MeshPass.
	 * @param collector Scene data collector.
	 * @param renderTargetId Target camera ID.
	 * @param viewport Normalized viewport [0..1].
	 * @param clearFlags Clear flags for this render target.
	 * @param backgroundColor Background color.
	 * @param cpuTarget Optional CPU readback target.
	 * @param frameUniforms Camera uniforms (view, projection, etc.).
	 */
	void renderToTexture(
		const RenderCollector &collector,
		uint64_t renderTargetId,
		const glm::vec4 &viewport,
		ClearFlags clearFlags,
		const glm::vec4 &backgroundColor,
		std::optional<TextureHandle> cpuTarget,
		const FrameUniforms &frameUniforms
	);

	/**
	 * @brief Composites multiple render targets onto the surface.
	 * Delegates to CompositePass, then optionally renders UI.
	 * @param uiCallback Optional UI rendering callback.
	 */
	void compositeTexturesToSurface(
		std::function<void(wgpu::RenderPassEncoder)> uiCallback = nullptr
	);

	// ========================================
	// Resource Management
	// ========================================

	/**
	 * @brief Creates or resizes render target textures.
	 * Handles both CPU-backed textures and dynamic viewport-sized targets.
	 * @param renderTargetId ID for caching.
	 * @param gpuTexture Existing GPU texture (may be null or outdated).
	 * @param cpuTarget Optional CPU-side texture source.
	 * @param viewport Normalized viewport dimensions.
	 * @param format Texture format.
	 * @param usageFlags Texture usage flags.
	 * @return Updated GPU texture.
	 */
	std::shared_ptr<webgpu::WebGPUTexture> updateRenderTexture(
		uint32_t renderTargetId,
		std::shared_ptr<webgpu::WebGPUTexture> &gpuTexture,
		const std::optional<Texture::Handle> &cpuTarget,
		const glm::vec4 &viewport,
		wgpu::TextureFormat format,
		wgpu::TextureUsage usageFlags
	);

	/**
	 * @brief Lazily prepares GPU resources for given render item indices.
	 */

	// ========================================
	// Deprecated / Debug Methods
	// ========================================

	void renderDebugPrimitives(
		wgpu::RenderPassEncoder renderPass,
		const DebugRenderCollector &debugCollector
	);

	std::shared_ptr<webgpu::WebGPUContext> m_context;
	std::unique_ptr<RenderPassManager> m_renderPassManager;
	std::unique_ptr<ShadowPass> m_shadowPass;
	std::unique_ptr<MeshPass> m_meshPass;
	std::unique_ptr<CompositePass> m_compositePass;

	FrameCache m_frameCache{};

	std::shared_ptr<webgpu::WebGPUTexture> m_surfaceTexture;
	std::shared_ptr<webgpu::WebGPUTexture> m_depthBuffer;

	std::unordered_map<uint64_t, RenderTarget> m_renderTargets;
};

} // namespace engine::rendering
