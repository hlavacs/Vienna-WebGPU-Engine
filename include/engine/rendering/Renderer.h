#pragma once

#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/CompositePass.h"
#include "engine/rendering/CompositionPass.h"
#include "engine/rendering/DebugPass.h"
#include "engine/rendering/DebugRenderCollector.h"
#include "engine/rendering/ForwardTransparencyPass.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameProfiler.h"
#include "engine/rendering/GBufferPass.h"
#include "engine/rendering/MeshPass.h"
#include "engine/rendering/ibl/BRDFLut.h"
#include "engine/rendering/ibl/PrefilteredEnv.h"
// #include "engine/rendering/RenderPassManager.h" ToDo: future use
#include "engine/rendering/ShadowPass.h"
#include "engine/rendering/SkyboxPass.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
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
struct RenderTarget;	   // forward declaration

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
	 * @brief Get the GBufferPass instance (deferred rendering).
	 * @return Reference to GBufferPass.
	 */
	GBufferPass *getGBufferPass() { return m_gBufferPass.get(); }

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
	 * @brief Updates the per-camera bind group used by the skybox pass.
	 *        Cached in @c m_skyboxBindGroups keyed by camera id.
	 *        SKYBOX and ENVIRONMENT layouts have the same binding shape but
	 *        come from different shaders, so the bind groups must be built
	 *        separately even though the underlying texture/sampler/uniform
	 *        data is identical.
	 */
	void updateSkyboxBindGroup(const RenderTarget &target);

	/**
	 * @brief Shared core of the two helpers above: build a bind group whose
	 *        layout matches the (sampler, environment texture, vec4 uniform)
	 *        triplet that both skybox and PBR environment use.
	 */
	std::shared_ptr<webgpu::WebGPUBindGroup> buildEnvironmentBindGroup(
		const std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> &layoutInfo,
		const RenderTarget &target,
		const char *debugLabel
	);

	/**
	 * @brief Build / refresh the consolidated Scene bind group for the given camera.
	 *
	 * Ten bindings: lights buffer (0), shadow comparison sampler (1),
	 * shadow 2D-array (2), shadow cube-array (3), shadow uniforms (4),
	 * environment uniforms (5), environment sampler (6), environment HDR
	 * equirect texture (7), cluster grid (8), cluster light indices (9).
	 * Resources are sourced from ShadowPass / SceneLightBuffer / ClusterManager;
	 * the env uniform UBO is owned per-camera in @c m_sceneEnvironmentBuffers
	 * and written here. Cached in @c m_sceneBindGroups keyed by camera id.
	 */
	void updateSceneBindGroup(const RenderTarget &target);

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
	std::unique_ptr<SkyboxPass> m_skyboxPass;
	std::unique_ptr<MeshPass> m_meshPass;
	std::unique_ptr<GBufferPass> m_gBufferPass;
	std::unique_ptr<DebugPass> m_debugPass;
	std::unique_ptr<CompositionPass> m_compositionPass;
	std::unique_ptr<ForwardTransparencyPass> m_transparencyPass;
	std::unique_ptr<CompositePass> m_compositePass;

	FrameProfiler m_profiler;

public:
	/// Read-only access to the frame profiler for UI display.
	[[nodiscard]] const FrameProfiler &getProfiler() const { return m_profiler; }

	/// The baked BRDF integration LUT used by IBL specular. nullptr until
	/// Renderer::initialize() bakes it. Sampled in WGSL via
	/// `vec2(NdotV, roughness)` to get the split-sum (scale, bias) terms.
	[[nodiscard]] const engine::rendering::ibl::BRDFLut &getBRDFLut() const { return m_brdfLut; }

	/// The pre-filtered environment mip chain used by IBL specular. Baked
	/// from whatever env texture the engine is using; rebakes lazily when
	/// the env source changes via prefilterEnvironment(). nullptr until
	/// the first successful bake. The IBL specular tap is:
	///   `textureSampleLevel(prefilteredEnv, smp, uv, roughness * maxMip)`.
	[[nodiscard]] const engine::rendering::ibl::PrefilteredEnv &getPrefilteredEnv() const { return m_prefilteredEnv; }

	/// (Re)bake the prefiltered environment from @p sourceEquirect. Called
	/// by the env-loading path when the scene's env texture changes.
	/// Idempotent for the same source — cheap to call.
	void prefilterEnvironment(const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect);

private:
	/// Build a representative render graph mirroring the per-camera frame
	/// flow, compile it, and log the resolved execution order. Called once
	/// at initialize() time so any dependency drift between the hand-coded
	/// orchestration and the graph's declared edges fails loud on startup.
	/// The graph itself is throwaway today — the integration step that
	/// makes it drive rendering replaces the manual sequence in renderFrame
	/// with graph.execute(ctx).
	void logFrameGraphLayout();

	FrameCache m_frameCache{};

	// Counts frames since last CacheRegistry::cleanAll() — the renderer
	// pumps notifyFrameAll() every frame but only runs the heavier
	// cleanAll() sweep every kCacheCleanInterval frames.
	uint32_t m_cacheCleanTick = 0;

	// Split-sum BRDF LUT — baked once during initialize(), sampled by the
	// IBL specular term in the deferred composition + PBR forward shaders.
	engine::rendering::ibl::BRDFLut m_brdfLut;

	// Pre-filtered env mip chain — baked from whatever env texture the
	// scene is using. The "source texture identity" we baked from is tracked
	// so we don't re-prefilter unnecessarily when the same env is supplied
	// twice in a row (scene reload, ImGui touch, ...).
	engine::rendering::ibl::PrefilteredEnv m_prefilteredEnv;
	WGPUTexture                            m_prefilteredEnvSource = nullptr;

	std::shared_ptr<webgpu::WebGPUTexture> m_surfaceTexture;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUTexture>> m_depthBuffers;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_skyboxBindGroups;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_sceneBindGroups;
	/// Per-camera environment-uniforms buffer (vec4 params) injected into the
	/// Scene bind group at @binding(5). Cached so its identity is stable
	/// across frames — the bind group keeps it valid and we can write env
	/// params directly via the wgpu queue.
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBuffer>> m_sceneEnvironmentBuffers;

	// Cross-frame cache of the last frame's render targets. Distinct from
	// m_frameCache.renderTargets (which is cleared between frames) because
	// onResize fires between frames and needs the camera viewport info to
	// resize depth buffers.
	std::unordered_map<uint64_t, RenderTarget> m_renderTargets;

	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_frameBindGroupLayout;
	std::shared_ptr<webgpu::WebGPUTexture> m_defaultEnvironmentTexture;
	uint32_t m_lastUploadedLightCount = std::numeric_limits<uint32_t>::max();
};

} // namespace engine::rendering
