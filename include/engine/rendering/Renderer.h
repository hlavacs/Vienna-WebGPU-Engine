#pragma once

#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendergraph/RenderGraph.h"
#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/cache/BindGroupSignature.h"
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
#include "engine/rendering/ibl/IrradianceMap.h"
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

	/**
	 * @brief Snapshot of every owned pass, in canonical render order.
	 *
	 * Returns raw pointers (non-owning) — the Renderer keeps the unique_ptrs.
	 * Used by the debug UI to drive per-pass enable/disable toggles without
	 * needing a getter per concrete pass type. The order matches the order
	 * passes execute in `renderToTexture` / `compositeTexturesToSurface`,
	 * so the UI can render them top-to-bottom and have it match the
	 * pipeline.
	 */
	std::vector<RenderPass *> getAllPasses();

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

	/// Per-camera RenderGraph that drives the deferred pass order
	/// (Shadow → GBuffer → ClusterCompute → Composition → Skybox →
	/// ForwardTransparency → Debug). Built lazily on the first
	/// `renderToTexture` from declared resource dependencies; each pass's
	/// `execute()` lambda captures `this` and reads per-camera state from
	/// @ref m_currentCamera. Shadow is the graph's first pass (it writes the
	/// shadow maps the Composition pass samples). Composite is NOT here — it
	/// runs once per frame after every camera and lives in @ref m_frameGraph.
	engine::rendergraph::RenderGraph                          m_perCameraGraph;
	bool                                                       m_perCameraGraphReady = false;

	/// Per-camera state the graph's pass lambdas read. Populated by
	/// `renderToTexture` *before* calling `m_perCameraGraph.execute(ctx)`,
	/// so the lambdas only need to capture `this` to reach everything
	/// the legacy hand-coded sequence had in scope.
	struct PerCameraContext
	{
		RenderTarget                                       *renderTarget   = nullptr;
		std::shared_ptr<webgpu::WebGPUTexture>              renderTexture;
		std::vector<size_t>                                 visibleIndices;
		const DebugRenderCollector                         *debugCollector = nullptr;
	};
	PerCameraContext m_currentCamera;

	/// Build the per-camera graph. Called once on first `renderToTexture`
	/// invocation (initialize() runs before m_passes have their shaders
	/// resolved; deferring to first frame keeps construction simple).
	void buildPerCameraGraph();

	/// Frame-level RenderGraph for the two once-per-frame stages: CameraViews
	/// (runs the per-camera graph for every camera) → Composite (tonemaps +
	/// blits the camera targets into the surface and draws UI). Composite
	/// genuinely runs after ALL cameras, so it can't live in the per-camera
	/// graph; this graph is its home. Built once in `initialize()`; its
	/// lambdas read frame-wide state from @ref m_currentFrame.
	engine::rendergraph::RenderGraph                                 m_frameGraph;
	bool                                                             m_frameGraphReady = false;

	/// Frame-wide state the frame graph's pass lambdas read. Populated by
	/// `renderFrame` *before* calling `m_frameGraph.execute(ctx)`, so the
	/// lambdas only need to capture `this`. Mirrors @ref m_currentCamera.
	struct FrameContext
	{
		const RenderCollector                       *collector       = nullptr;
		const DebugRenderCollector                  *debugCollector  = nullptr;
		const std::vector<BindGroupDataProvider>    *customProviders = nullptr;
		std::function<void(wgpu::RenderPassEncoder)> uiCallback;
	};
	FrameContext m_currentFrame;

	/// Build the frame-level graph (CameraViews → Composite). Called once
	/// from `initialize()`.
	void buildFrameGraph();

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

	/// The Lambertian diffuse irradiance map. Sampled at the surface normal
	/// to get the radiometrically-correct diffuse IBL term — no clamp or
	/// hand-rolled hemisphere convolution at the shader call site.
	[[nodiscard]] const engine::rendering::ibl::IrradianceMap &getIrradianceMap() const { return m_irradianceMap; }

	/// Drop every Renderer-side resource that references a factory-cached
	/// object. Use this together with `cacheRegistry().clearAll()`: when
	/// caches are dropped, the wgpu IDs they handed out get released, but
	/// our cached scene / skybox bind groups still hold those dead IDs
	/// inside their internal descriptors — the very next draw crashes with
	/// "Sampler[Id] does not exist" or similar. This drops the bind groups
	/// + env-source markers so the next frame rebuilds them from the
	/// re-baked factory state.
	///
	/// Does NOT touch the IBL textures themselves (they're owned here, not
	/// in the factory cache), but clearing the env-source marker forces
	/// prefilterEnvironment() to re-bake on next call.
	void resetCachedBindings();

private:
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

	// Cosine-weighted diffuse irradiance map. Same per-env lifetime as
	// PrefilteredEnv — we bake both off the same source whenever the env
	// changes, and share the "did the source change" check.
	engine::rendering::ibl::IrradianceMap m_irradianceMap;

	std::shared_ptr<webgpu::WebGPUTexture> m_surfaceTexture;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUTexture>> m_depthBuffers;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_skyboxBindGroups;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_sceneBindGroups;
	/// Per-camera identity-signature of the resources baked into the cached
	/// Scene / Skybox bind groups. Each frame, `updateSceneBindGroup` /
	/// `updateSkyboxBindGroup` recomputes the current signature from the
	/// live constituents (lights buffer, shadow textures, env texture, etc.)
	/// and rebuilds the bind group only if it differs. Replaces the
	/// historical "rebuild every frame for every camera" leak — wgpu
	/// internally refcounts every bind group's resources, so freshly
	/// allocating one per frame held the entire previous frame's GPU state
	/// alive until the GC eventually ran.
	std::unordered_map<uint64_t, engine::rendering::cache::BindGroupSignature> m_sceneBindGroupSignatures;
	std::unordered_map<uint64_t, engine::rendering::cache::BindGroupSignature> m_skyboxBindGroupSignatures;
	/// Per-camera environment-uniforms buffer (vec4 params) injected into the
	/// Scene bind group at @binding(5). Cached so its identity is stable
	/// across frames — the bind group keeps it valid and we can write env
	/// params directly via the wgpu queue.
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBuffer>> m_sceneEnvironmentBuffers;

	/// Per-camera cached frustum-cull result. Skip the O(N items) cull when
	/// the camera matrix AND the collector's item count both match the
	/// previous frame's. Idle / paused / scripted-view frames pay zero
	/// CPU for visibility determination. The fingerprint deliberately does
	/// NOT include per-item transforms — that would scale with item count
	/// just like the cull itself does — so the rare case of "object moves
	/// while camera is locked and item count is stable" reuses last frame's
	/// visibility set for one frame. For typical scene-motion velocities
	/// the slip is invisible.
	struct CullCache
	{
		glm::mat4           lastViewProjection{};
		std::size_t         lastItemsCount = 0;
		std::vector<size_t> visibleIndices;
		bool                valid          = false;
	};
	std::unordered_map<uint64_t, CullCache> m_cullCaches;

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
