#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/CompositePass.h"
#include "engine/rendering/DebugPass.h"
#include "engine/rendering/DebugRenderCollector.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/MeshPass.h"
#include "engine/rendering/Model.h"
// #include "engine/rendering/RenderPassManager.h" ToDo: future use
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/ShadowPass.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine
{
class GameEngine; // forward declaration
} // namespace engine

namespace engine::rendering
{
namespace webgpu
{
class WebGPUContext; // forward declaration
} // namespace webgpu

class RenderCollector; // forward declaration
class RenderTarget;	   // forward declaration

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
		const DebugRenderCollector &debugRenderCollector,
		float time,
		const std::vector<BindGroupDataProvider> &customBindGroupProviders,
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

	/**
	 * @brief Get the ShadowPass instance.
	 * @return Reference to ShadowPass.
	 */
	ShadowPass &getShadowPass() { return *m_shadowPass; }

	/**
	 * @brief Get the MeshPass instance.
	 * @return Reference to MeshPass.
	 */
	MeshPass &getMeshPass() { return *m_meshPass; }

	/**
	 * @brief Get the CompositePass instance.
	 * @return Reference to CompositePass.
	 */
	CompositePass &getCompositePass() { return *m_compositePass; }

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
	 * @param debugCollector Debug primitives collector.
	 * @param renderTarget Render target information for this camera.
	 */
	void renderToTexture(
		const RenderCollector &collector,
		const DebugRenderCollector &debugCollector,
		RenderTarget &renderTarget,
		const std::vector<BindGroupDataProvider> &customBindGroupProviders
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
	 * @brief Updates or creates frame bind group for a render target.
	 * @param target Render target to update frame uniforms for.
	 * @param time Current frame time.
	 */
	void updateFrameBindGroup(const RenderTarget &target, float time);

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
		const math::Rect &viewport,
		wgpu::TextureFormat format,
		wgpu::TextureUsage usageFlags
	);

	std::shared_ptr<webgpu::WebGPUContext> m_context;
	// std::unique_ptr<RenderPassManager> m_renderPassManager; // ToDo: future use
	std::unique_ptr<ShadowPass> m_shadowPass;
	std::unique_ptr<MeshPass> m_meshPass;
	std::unique_ptr<DebugPass> m_debugPass;
	std::unique_ptr<CompositePass> m_compositePass;

	FrameCache m_frameCache{};

	std::shared_ptr<webgpu::WebGPUTexture> m_surfaceTexture;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUTexture>> m_depthBuffers;

	std::unordered_map<uint64_t, RenderTarget> m_renderTargets;

	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_frameBindGroupLayout;
};

} // namespace engine::rendering
