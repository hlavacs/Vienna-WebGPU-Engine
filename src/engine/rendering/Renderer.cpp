#include "engine/rendering/Renderer.h"

#include <limits>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "engine/lighting/LightManager.h"
#include "engine/rendering/BindGroupDataProvider.h"
#include "engine/rendering/CacheStats.h"
#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/ClusterManager.h"
#include "engine/rendering/ColorSpace.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/EnvironmentUniforms.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/RenderTarget.h"
#include "engine/rendering/SceneLightBuffer.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/GBuffer.h"
#include "engine/rendergraph/RenderGraph.h"
#include "engine/rendering/ibl/BRDFLut.h"
#include "engine/rendering/ibl/PrefilteredEnv.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering
{

Renderer::Renderer(std::shared_ptr<webgpu::WebGPUContext> context) :
	m_context(context)
// m_renderPassManager(std::make_unique<RenderPassManager>(*context))
{
}

Renderer::~Renderer() = default;

bool Renderer::initialize()
{
	spdlog::info("Initializing Renderer");

	// Profiler must be wired into the context BEFORE passes initialise so any
	// startup encoder also has the option to write timestamps. initGpu is a
	// no-op when the device lacks `timestamp-query`.
	m_context->setFrameProfiler(&m_profiler);
	m_profiler.initGpu(*m_context);

	// Initialize rendering passes
	m_shadowPass = std::make_unique<ShadowPass>(m_context);
	if (!m_shadowPass->initialize())
	{
		spdlog::error("Failed to initialize ShadowPass");
		return false;
	}

	m_meshPass = std::make_unique<MeshPass>(m_context);
	if (!m_meshPass->initialize())
	{
		spdlog::error("Failed to initialize MeshPass");
		return false;
	}

	// G-buffer starts at a placeholder size; it is resized to match the
	// active render target on the first frame so we never waste textures
	// at the default 1x1 nor strand them at a hardcoded 1920x1080.
	m_gBufferPass = std::make_unique<GBufferPass>(m_context, 1, 1);
	if (!m_gBufferPass->initialize())
	{
		spdlog::error("Failed to initialize GBufferPass");
		return false;
	}

	m_compositionPass = std::make_unique<CompositionPass>(m_context);
	if (!m_compositionPass->initialize())
	{
		spdlog::error("Failed to initialize CompositionPass");
		return false;
	}

	m_skyboxPass = std::make_unique<SkyboxPass>(m_context);
	if (!m_skyboxPass->initialize())
	{
		spdlog::error("Failed to initialize SkyboxPass");
		return false;
	}

	// Forward transparency runs after deferred composition + skybox so it can
	// blend onto the lit HDR result while reading the G-buffer depth read-only.
	m_transparencyPass = std::make_unique<ForwardTransparencyPass>(m_context);
	if (!m_transparencyPass->initialize())
	{
		spdlog::error("Failed to initialize ForwardTransparencyPass");
		return false;
	}

	m_debugPass = std::make_unique<DebugPass>(m_context);
	if (!m_debugPass->initialize())
	{
		spdlog::error("Failed to initialize DebugPass");
		return false;
	}

	// Tutorial 04 - Step 9: Initialize PostProcessingPass

	m_compositePass = std::make_unique<CompositePass>(m_context);
	if (!m_compositePass->initialize())
	{
		spdlog::error("Failed to initialize CompositePass");
		return false;
	}

	m_frameBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout(bindgroup::defaults::FRAME);
	if (!m_frameBindGroupLayout)
	{
		spdlog::error("Failed to get global bind group layout for frame uniforms");
		return false;
	}

	// Skybox layout is fetched fresh from its shader inside updateSkyboxBindGroup
	// so hot-reloading a shader can't leave us holding a stale layout.
	m_defaultEnvironmentTexture = m_context->textureFactory().createFromColor(
		glm::vec3(0.0f),
		1,
		1,
		ColorSpace::Linear
	);

	// Build the frame-level render graph (CameraViews -> Composite) that
	// drives the once-per-frame stages. Compiled here so any dependency
	// drift fails loud at startup; executed every frame from renderFrame.
	buildFrameGraph();

	// Bake the split-sum BRDF integration LUT once at startup. The texture
	// is global (not per-scene); downstream IBL specular code samples it
	// via Renderer::getBRDFLut(). Cheap (256x256 RG16Float, ~256 KB) and
	// only runs at init — no per-frame cost.
	if (!m_brdfLut.initialize(*m_context))
	{
		spdlog::warn("BRDF LUT bake failed — IBL specular fallback will be flat");
	}

	spdlog::info("Renderer initialized successfully");
	return true;
}

void Renderer::prefilterEnvironment(const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect)
{
	if (!sourceEquirect) return;

	// Identity by raw wgpu handle: shared_ptr wrappers swap when the factory
	// hot-reloads but the underlying texture stays the same. If the same
	// source comes through twice in a row, the prior bake is still valid.
	WGPUTexture rawHandle = static_cast<WGPUTexture>(sourceEquirect->getTexture());
	if (rawHandle == m_prefilteredEnvSource && m_prefilteredEnv.getTexture()) return;

	if (!m_prefilteredEnv.bake(*m_context, sourceEquirect))
	{
		spdlog::warn("Renderer: env prefilter bake failed — IBL specular falls back to flat env");
		return;
	}
	// Same env source produces both prefilter (specular) and irradiance map
	// (diffuse), so bake them together — if one fails the other still has a
	// chance to produce a valid texture.
	if (!m_irradianceMap.bake(*m_context, sourceEquirect))
	{
		spdlog::warn("Renderer: env irradiance bake failed — IBL diffuse falls back to raw env sample");
	}
	m_prefilteredEnvSource = rawHandle;
}

void Renderer::resetCachedBindings()
{
	// Per-camera Scene / Skybox bind groups capture wgpu Sampler IDs (the
	// shadow sampler from ShadowPass, the environment sampler from
	// SamplerFactory). When the cache registry's clearAll() drops those
	// samplers, the IDs become dangling and the next setBindGroup() call
	// hits a wgpu panic. Drop the bind groups + their backing env-uniform
	// buffers so updateSceneBindGroup() rebuilds everything from fresh
	// factory state next frame.
	m_skyboxBindGroups.clear();
	m_sceneBindGroups.clear();
	// Drop signatures too, otherwise the cache-hit fast path in
	// update{Scene,Skybox}BindGroup would compare against a stale signature
	// and skip the rebuild that just dropped the cached bind group.
	m_skyboxBindGroupSignatures.clear();
	m_sceneBindGroupSignatures.clear();
	m_sceneEnvironmentBuffers.clear();

	// Re-arm prefilterEnvironment: the IBL textures stay alive (they're
	// owned here, not by the factory), but we want the next env supply to
	// re-bake them now that the sampler used during the original bake has
	// been released. Without this the prior bake would be reused with a
	// fresh sampler binding it — usually fine, but during the clear-all
	// flow the user expects a true reset.
	m_prefilteredEnvSource = nullptr;

	// Depth buffers reference render-target identity from the texture
	// factory's cache. After clearAll() the next frame would try to bind
	// the old wgpu IDs. Dropping these forces renderToTexture's resize
	// path to rebuild them.
	m_depthBuffers.clear();

	// Drop every cached per-object/per-material/per-custom bind group too.
	// The factory soft-clear that just ran may have rebuilt the underlying
	// resources (samplers, textures, materials); the cached bundles point
	// at the stale pointers, so the next frame's fast-path in
	// FrameCache::prepareGPUResources would happily reuse them and bind
	// dead views. Full clear() invalidates the lot and forces the next
	// frame to re-prepare from scratch — exactly the semantic Clear All
	// wants.
	m_frameCache.clear();

	// Frustum-cull caches reference visible-indices that may now name
	// items the FrameCache::clear() above just dropped. Invalidate so the
	// next frame re-cuts from the fresh items list.
	m_cullCaches.clear();
}

void Renderer::reloadShaders()
{
	// Reload shader sources + rebuild pipelines, THEN drop cached bind groups.
	// reloadAllPipelines() only soft-clears the pipeline cache; the renderer's
	// cached scene/skybox/object/material bind groups still point at the old
	// shader layouts, so deferred-rendered meshes (e.g. the SeaKeep building)
	// render wrong until a scene change calls resetCachedBindings(). Doing the
	// reset here makes a plain shader reload behave like that scene switch.
	m_context->shaderRegistry().reloadAllShaders();
	m_context->pipelineManager().reloadAllPipelines();
	resetCachedBindings();

	// Each render pass ALSO caches its own pipeline + bind groups keyed by
	// resource identity, not the shader version — e.g. CompositionPass's
	// G-buffer bind group. After a reload the shader's bind-group layout changes
	// but those per-pass caches don't notice, so the pass binds an old-layout
	// group to the new pipeline and the draw produces nothing (the deferred
	// opaque geometry vanishes, while the forward transparency pass — which
	// re-syncs via the material path — still draws). cleanup() resets each pass
	// so it rebuilds against the reloaded shaders next frame; this is exactly
	// what the resize path already does.
	for (auto *pass : getAllPasses())
		if (pass) pass->cleanup();
	if (m_compositePass) m_compositePass->cleanup();
}

namespace
{

std::string joinPassNames(const std::vector<std::string> &names)
{
	// ASCII separator — Windows console default code page mangles UTF-8
	// arrows ('→' shows as 'ÔåÆ' on cp1252). spdlog doesn't force the
	// console to UTF-8 either, so we keep the log readable by sticking to
	// 7-bit ASCII for the separator.
	std::string trace;
	for (const auto &n : names)
	{
		if (!trace.empty()) trace += " -> ";
		trace += n;
	}
	return trace;
}

} // namespace

void Renderer::buildFrameGraph()
{
	using namespace engine::rendergraph;

	// Frame-level graph for the two once-per-frame stages. CameraViews runs
	// the per-camera graph (Shadow -> GBuffer -> ... -> Debug) for every
	// camera; Composite then tonemaps + blits the camera targets into the
	// surface and draws UI. Composite genuinely runs AFTER all cameras have
	// rendered, so it can't live in the per-camera graph — this two-node
	// graph is its home, and the cameraColor read/write makes the "cameras
	// before composite" edge data-driven instead of hand-sequenced.
	auto cameraColor = m_frameGraph.addImported("CameraColor", ResourceType::ColorTexture);
	auto backbuffer  = m_frameGraph.addImported("Backbuffer",  ResourceType::ColorTexture);

	m_frameGraph.addPass(std::make_unique<FunctionPass>(
		"CameraViews",
		std::vector<ResourceHandle>{},
		std::vector<ResourceHandle>{cameraColor},
		[this](RenderContext &) {
			// The scene collector is the same for every camera, so hand it
			// to the shadow pass once before the loop. Each camera then
			// renders its own shadow maps + scene through the per-camera
			// graph invoked inside renderToTexture.
			m_shadowPass->setRenderCollector(m_currentFrame.collector);
			for (auto &[cameraId, target] : m_frameCache.renderTargets)
			{
				renderToTexture(
					*m_currentFrame.collector,
					*m_currentFrame.debugCollector,
					target,
					*m_currentFrame.customProviders);
			}
		}));

	m_frameGraph.addPass(std::make_unique<FunctionPass>(
		"Composite",
		std::vector<ResourceHandle>{cameraColor},
		std::vector<ResourceHandle>{backbuffer},
		[this](RenderContext &) {
			FrameProfiler::Scope s(m_profiler, "Pass.Composite+UI");
			compositeTexturesToSurface(m_currentFrame.uiCallback);
		}));

	auto result = m_frameGraph.compile();
	if (!result.success)
	{
		spdlog::error("Frame render graph compile FAILED: {}", result.error);
		return;
	}
	m_frameGraphReady = true;
	spdlog::info("Frame render graph compiled: order = {}",
		joinPassNames(m_frameGraph.compiledOrder()));
}

void Renderer::buildPerCameraGraph()
{
	using namespace engine::rendergraph;

	// Import the (logical) resources our deferred chain produces and consumes.
	// The graph uses these only to compute execution order via reads/writes —
	// actual GPU resources are owned by the passes themselves (GBuffer owns
	// its attachments; ClusterManager owns its buffers; CompositionPass writes
	// to whatever render target the per-camera context hands it).
	auto shadowMaps   = m_perCameraGraph.addImported("ShadowMaps",     ResourceType::DepthTexture);
	auto shadowUniform= m_perCameraGraph.addImported("ShadowUniform",  ResourceType::Buffer);
	auto gbufferColor = m_perCameraGraph.addImported("GBuffer.Color",  ResourceType::ColorTexture);
	auto gbufferDepth = m_perCameraGraph.addImported("GBuffer.Depth",  ResourceType::DepthTexture);
	auto clusterGrid  = m_perCameraGraph.addImported("ClusterGrid",    ResourceType::Buffer);
	auto clusterIdx   = m_perCameraGraph.addImported("ClusterIndices", ResourceType::Buffer);
	auto litHdr       = m_perCameraGraph.addImported("LitHDR",         ResourceType::ColorTexture);

	// Each FunctionPass lambda captures `this` and reads per-camera state
	// from `m_currentCamera`, which `renderToTexture` populates before
	// calling `m_perCameraGraph.execute(ctx)`. The lambda body is what used
	// to be a hard-coded block in `renderToTexture` — moving it here makes
	// pass ordering data-driven (the graph compiles edges from the
	// reads/writes lists below) and lets the existing `isEnabled()` toggles
	// gate the whole pass uniformly.

	// Shadow runs first: it renders the light-POV depth maps that the
	// Composition pass samples. It's self-contained w.r.t. GPU resource
	// prep (it culls and prepares its own per-light visible item sets), so
	// its only ordering constraint is "before Composition" — expressed by
	// the shadowMaps / shadowUniform write here and the matching read on
	// Composition below. Registered first so the topo-sort tie-break (FIFO
	// on registration order) keeps it at the front.
	m_perCameraGraph.addPass(std::make_unique<FunctionPass>(
		"Shadow",
		std::vector<ResourceHandle>{},
		std::vector<ResourceHandle>{shadowMaps, shadowUniform},
		[this](RenderContext &) {
			auto &renderTarget = *m_currentCamera.renderTarget;
			m_shadowPass->setCameraId(renderTarget.cameraId);
			if (m_shadowPass->isEnabled())
			{
				FrameProfiler::Scope s(m_profiler, "Pass.Shadow");
				m_shadowPass->render(m_frameCache);
			}
		}));

	m_perCameraGraph.addPass(std::make_unique<FunctionPass>(
		"GBuffer",
		std::vector<ResourceHandle>{},
		std::vector<ResourceHandle>{gbufferColor, gbufferDepth},
		[this](RenderContext &) {
			auto &renderTarget = *m_currentCamera.renderTarget;
			auto  cameraId     = renderTarget.cameraId;
			m_gBufferPass->resize(
				m_currentCamera.renderTexture->getWidth(),
				m_currentCamera.renderTexture->getHeight());
			m_gBufferPass->setCameraId(cameraId);
			m_gBufferPass->setVisibleIndices(m_currentCamera.visibleIndices);
			if (m_gBufferPass->isEnabled())
			{
				FrameProfiler::Scope s(m_profiler, "Pass.GBuffer");
				m_gBufferPass->render(m_frameCache);
			}
		}));

	m_perCameraGraph.addPass(std::make_unique<FunctionPass>(
		"ClusterCompute",
		std::vector<ResourceHandle>{},
		std::vector<ResourceHandle>{clusterGrid, clusterIdx},
		[this](RenderContext &) {
			auto &renderTarget    = *m_currentCamera.renderTarget;
			auto  cameraId        = renderTarget.cameraId;
			auto  sceneLightBuf   = m_context->sceneLightBuffer();
			auto  clusterManager  = m_context->clusterManager();
			if (sceneLightBuf && sceneLightBuf->getBufferWrapped() && clusterManager)
			{
				FrameProfiler::Scope s(m_profiler, "Pass.ClusterCompute");
				if (!clusterManager->assignLights(
						cameraId,
						renderTarget.viewProjectionMatrix,
						m_frameCache,
						sceneLightBuf->getLightCount()))
					spdlog::warn("Failed to assign lights to clusters");
			}
		}));

	m_perCameraGraph.addPass(std::make_unique<FunctionPass>(
		"Composition",
		std::vector<ResourceHandle>{gbufferColor, gbufferDepth, shadowMaps, shadowUniform, clusterGrid, clusterIdx},
		std::vector<ResourceHandle>{litHdr},
		[this](RenderContext &) {
			auto &renderTarget = *m_currentCamera.renderTarget;
			auto  cameraId     = renderTarget.cameraId;

			// Scene bind group binds the cluster output and the lit env;
			// has to happen AFTER ClusterCompute (the read declaration on
			// clusterGrid above is what the graph uses to enforce that).
			{
				FrameProfiler::Scope s(m_profiler, "Frame.SceneBindGroup");
				updateSceneBindGroup(renderTarget);
			}

			auto sceneBindGroup = m_sceneBindGroups[cameraId];
			if (!sceneBindGroup)
			{
				spdlog::warn("CompositionPass skipped: scene bind group failed to build");
				return;
			}

			auto compositionPassContext = m_context->renderPassFactory().create(
				m_currentCamera.renderTexture,
				nullptr,
				ClearFlags::SolidColor,
				renderTarget.backgroundColor
			);

			m_compositePass->setHDREnabled(renderTarget.hdr);
			m_compositionPass->setRenderPassContext(compositionPassContext);
			m_compositionPass->setGBuffer(&m_gBufferPass->getGBuffer());
			m_compositionPass->setSceneBindGroup(sceneBindGroup);
			m_compositionPass->setCameraId(cameraId);
			if (m_compositionPass->isEnabled())
			{
				FrameProfiler::Scope s(m_profiler, "Pass.Composition");
				m_compositionPass->render(m_frameCache);
			}
		}));

	m_perCameraGraph.addPass(std::make_unique<FunctionPass>(
		"Skybox",
		std::vector<ResourceHandle>{gbufferDepth, litHdr},
		std::vector<ResourceHandle>{litHdr},
		[this](RenderContext &) {
			auto &renderTarget = *m_currentCamera.renderTarget;
			auto  cameraId     = renderTarget.cameraId;
			if (!renderTarget.skyboxEnabled) return;

			updateSkyboxBindGroup(renderTarget);
			auto skyboxBindGroup = m_skyboxBindGroups[cameraId];
			if (!skyboxBindGroup) return;

			auto &gBuffer = m_gBufferPass->getGBuffer();
			auto  skyboxPassContext = m_context->renderPassFactory().create(
				m_currentCamera.renderTexture,
				gBuffer.getDepthTexture(),
				ClearFlags::None,
				renderTarget.backgroundColor
			);
			m_skyboxPass->setRenderPassContext(skyboxPassContext);
			m_skyboxPass->setCameraId(cameraId);
			m_skyboxPass->setEnvironmentBindGroup(skyboxBindGroup);
			if (m_skyboxPass->isEnabled())
			{
				FrameProfiler::Scope s(m_profiler, "Pass.Skybox");
				m_skyboxPass->render(m_frameCache);
			}
		}));

	m_perCameraGraph.addPass(std::make_unique<FunctionPass>(
		"ForwardTransparency",
		std::vector<ResourceHandle>{gbufferDepth, litHdr},
		std::vector<ResourceHandle>{litHdr},
		[this](RenderContext &) {
			if (!m_transparencyPass) return;
			auto &renderTarget = *m_currentCamera.renderTarget;
			auto  cameraId     = renderTarget.cameraId;
			auto  sceneBindGroup = m_sceneBindGroups[cameraId];
			if (!sceneBindGroup) return;

			auto &gBuffer = m_gBufferPass->getGBuffer();
			auto  transparencyPassContext = m_context->renderPassFactory().create(
				m_currentCamera.renderTexture,
				gBuffer.getDepthTexture(),
				ClearFlags::None,
				renderTarget.backgroundColor
			);
			m_transparencyPass->setRenderPassContext(transparencyPassContext);
			m_transparencyPass->setCameraId(cameraId);
			m_transparencyPass->setCameraPosition(renderTarget.cameraPosition);
			m_transparencyPass->setVisibleIndices(m_currentCamera.visibleIndices);
			m_transparencyPass->setSceneBindGroup(sceneBindGroup);
			if (m_transparencyPass->isEnabled())
			{
				FrameProfiler::Scope s(m_profiler, "Pass.ForwardTransparency");
				m_transparencyPass->render(m_frameCache);
			}
		}));

	m_perCameraGraph.addPass(std::make_unique<FunctionPass>(
		"Debug",
		std::vector<ResourceHandle>{litHdr},
		std::vector<ResourceHandle>{litHdr},
		[this](RenderContext &) {
			auto &renderTarget = *m_currentCamera.renderTarget;
			auto  cameraId     = renderTarget.cameraId;
			auto  debugPassContext = m_context->renderPassFactory().create(
				m_currentCamera.renderTexture,
				nullptr,
				ClearFlags::None,
				renderTarget.backgroundColor
			);
			m_debugPass->setRenderPassContext(debugPassContext);
			m_debugPass->setCameraId(cameraId);
			m_debugPass->setDebugCollector(m_currentCamera.debugCollector);
			if (m_debugPass->isEnabled())
			{
				FrameProfiler::Scope s(m_profiler, "Pass.Debug");
				m_debugPass->render(m_frameCache);
			}
		}));

	auto result = m_perCameraGraph.compile();
	if (!result.success)
	{
		spdlog::error("Per-camera render graph compile FAILED: {}", result.error);
		return;
	}
	m_perCameraGraphReady = true;
	spdlog::info("Per-camera render graph compiled: order = {}",
		joinPassNames(m_perCameraGraph.compiledOrder()));
}

bool Renderer::renderFrame(
	std::vector<RenderTarget> &renderTargets,
	const RenderCollector &renderCollector,
	const DebugRenderCollector &debugRenderCollector,
	float time,
	const std::vector<BindGroupDataProvider> &customBindGroupProviders,
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	m_profiler.beginFrame();
	FrameProfiler::Scope frameScope(m_profiler, "Frame.Total");

	// === PHASE 1: Frame Initialization ===
	{
		FrameProfiler::Scope s(m_profiler, "Frame.StartFrame");
		startFrame();
	}
	if (renderTargets.empty())
	{
		spdlog::warn("renderFrame called with no render targets");
		return false;
	}

	// === PHASE 2: Prepare Render Targets ===
	std::unordered_map<uint64_t, RenderTarget> uniqueRenderTargets;
	{
		FrameProfiler::Scope s(m_profiler, "Frame.UpdateFrameBindGroups");
		for (const auto &target : renderTargets)
		{
			if (uniqueRenderTargets.count(target.cameraId))
			{
				spdlog::warn("Duplicate render target for cameraId {} detected; skipping", target.cameraId);
				continue;
			}
			uniqueRenderTargets[target.cameraId] = target;
			updateFrameBindGroup(target, time);
		}
	}

	// === PHASE 3: Setup Frame-Wide Data ===
	// FrameCache stores data shared across all render passes in this frame:
	// - Lights (directional, point, spot)
	// - Render targets (camera viewports)
	// - Shadow maps and shadow matrices
	// - GPU resources (meshes, materials)
	m_frameCache.lights = renderCollector.getLights();
	m_frameCache.renderTargets = std::move(uniqueRenderTargets);
	m_frameCache.time = time;
	// Keep a separate copy that survives FrameCache::clear() so onResize
	// (which fires between frames) still has per-camera viewport info to
	// resize depth buffers against.
	m_renderTargets = m_frameCache.renderTargets;

	// Extract light data and determine which lights need shadow maps
	{
		FrameProfiler::Scope s(m_profiler, "Frame.ExtractLights");
		auto [lightUniforms, shadowRequests] = renderCollector.extractLightsAndShadows(
			constants::MAX_SHADOW_MAPS_2D,
			constants::MAX_SHADOW_MAPS_CUBE
		);
		m_frameCache.lightUniforms = std::move(lightUniforms);
		m_frameCache.shadowRequests = std::move(shadowRequests);
	}

	// Upload current frame lights to the shared scene light storage buffer for deferred composition.
	if (auto lightManager = m_context->lightManager())
	{
		FrameProfiler::Scope s(m_profiler, "Frame.UploadLights");
		lightManager->updateLights(renderCollector);
		if (auto sceneLightBuffer = m_context->sceneLightBuffer())
		{
			sceneLightBuffer->updateFromLights(m_frameCache.lightUniforms);
			const uint32_t uploadedLightCount = sceneLightBuffer->getLightCount();
			if (uploadedLightCount != m_lastUploadedLightCount)
			{
				spdlog::info(
					"Deferred light counts changed: collector={}, uniforms={}, uploaded={}",
					renderCollector.getLightCount(),
					m_frameCache.lightUniforms.size(),
					uploadedLightCount
				);
				m_lastUploadedLightCount = uploadedLightCount;
			}
			if (lightManager->getLightCount() == 0 && !m_frameCache.lights.empty())
			{
				spdlog::warn("Deferred lighting upload produced 0 lights from {} scene lights", m_frameCache.lights.size());
			}
		}
		else
		{
			spdlog::warn("SceneLightBuffer unavailable - deferred composition will render without lights");
		}
	}
	else
	{
		spdlog::warn("LightManager unavailable - deferred composition will render without lights");
	}

	// === PHASE 4 + 5: Render every camera view, then composite to surface ===
	// Both stages are driven by the frame-level render graph (CameraViews ->
	// Composite). CameraViews runs the per-camera graph for each camera
	// (Shadow -> GBuffer -> ... -> Debug); Composite tonemaps + blits the
	// results into the surface and draws UI. The graph lambdas read the
	// frame-wide collectors / UI callback from m_currentFrame, so we publish
	// them here for the duration of execute() (mirrors the m_currentCamera
	// pattern the per-camera graph uses).
	if (m_frameGraphReady)
	{
		m_currentFrame.collector       = &renderCollector;
		m_currentFrame.debugCollector  = &debugRenderCollector;
		m_currentFrame.customProviders = &customBindGroupProviders;
		m_currentFrame.uiCallback      = uiCallback;

		engine::rendergraph::RenderContext ctx;
		m_frameGraph.execute(ctx);

		m_currentFrame = {};
	}
	// Resolve any pending GPU timestamps from this frame's passes. Must come
	// AFTER every pass submission but BEFORE present so the queue still has
	// the per-pass encoders ahead of the resolve copy.
	m_profiler.resolveGpuTimestamps(*m_context);

	{
		FrameProfiler::Scope s(m_profiler, "Frame.Present");
		m_context->getSurface().present();
	}
	m_surfaceTexture.reset();

	// === PHASE 6: Post-Frame Cleanup ===
	// Per-frame "soft" reset — drops only the truly per-frame data (lights,
	// shadow requests, render targets, time). The GPU resource bundles
	// (gpuRenderItems, customBindGroupCache, frame/objectBindGroupCache)
	// persist across frames so prepareGPUResources / processBindGroupProviders
	// can skip the factory+bind-group rebuild for objects that haven't
	// changed since the last frame. The full clear() (drops every cache)
	// is reserved for scene change / Clear All flows.
	m_frameCache.endFrame();

	// Cache lifecycle: notifyFrameAll() bumps the frame counter on every
	// registered factory cache (drives age-based eviction). cleanAll() runs
	// every kCacheCleanInterval frames — a periodic sweep that actually
	// evicts stale entries. Both are no-ops for factories that didn't opt
	// in to age-eviction, so this is safe to call unconditionally.
	m_context->cacheRegistry().notifyFrameAll();
	static constexpr uint32_t kCacheCleanInterval = 60; // ~1 second at 60fps
	if ((++m_cacheCleanTick % kCacheCleanInterval) == 0)
	{
		m_context->cacheRegistry().cleanAll();
	}

	return true;
}

void Renderer::startFrame()
{
	m_surfaceTexture = m_context->surfaceManager().acquireNextTexture();

	if (!m_surfaceTexture)
	{
		spdlog::error("Failed to acquire surface texture");
		return;
	}

	if (m_surfaceTexture->getWidth() == 0 || m_surfaceTexture->getHeight() == 0)
	{
		spdlog::warn("Surface texture has invalid dimensions: {}x{}", m_surfaceTexture->getWidth(), m_surfaceTexture->getHeight());
		return;
	}

	// gpuRenderItems intentionally NOT cleared here — that defeats the
	// "if (already_prepared) skip" short-circuit in prepareGPUResources
	// and forced every visible item's GPU bundle (model/mesh/material
	// factory lookups + object bind group + uniform writeBuffer) to be
	// rebuilt every frame, even for static scenes. The fast path inside
	// prepareGPUResources walks the objectID + transform delta to keep
	// the buffer in sync without the rebuild.
}

void Renderer::updateFrameBindGroup(const RenderTarget &target, float time)
{
	// Bind groups are WebGPU's way of grouping resources (buffers, textures, samplers)
	// that shaders need to access. They're organized into "sets" for efficient binding.
	// This bind group contains per-frame, per-camera data (Group 0 in shader).

	// Create bind group on first use for this camera
	if (!m_frameCache.frameBindGroupCache[target.cameraId])
	{
		m_frameCache.frameBindGroupCache[target.cameraId] =
			m_context->bindGroupFactory().createBindGroup(m_frameBindGroupLayout);
	}

	// Build frame uniforms structure with camera matrices and time
	FrameUniforms frameUniforms = target.getFrameUniforms(time);
	// Update GPU buffer with new uniform data for this frame
	// Binding 0 in the frame bind group = uniform buffer with camera data
	m_frameCache.frameBindGroupCache[target.cameraId]->updateBuffer(0, &frameUniforms, sizeof(FrameUniforms), 0);
}

std::shared_ptr<webgpu::WebGPUBindGroup> Renderer::buildEnvironmentBindGroup(
	const std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> &layoutInfo,
	const RenderTarget &target,
	const char *debugLabel
)
{
	if (!layoutInfo)
		return nullptr;

	std::shared_ptr<webgpu::WebGPUTexture> environmentTexture = m_defaultEnvironmentTexture;
	if (target.environmentTexture.has_value() && target.environmentTexture->valid())
	{
		// Environment maps are HDR equirect data - decode linearly, not sRGB.
		webgpu::WebGPUTextureOptions options{};
		options.colorSpace = ColorSpace::Linear;
		auto texture = m_context->textureFactory().createFromHandle(target.environmentTexture.value(), options);
		if (texture)
			environmentTexture = texture;
	}

	// IBL (irradianceEnabled) and the visible skybox are independent toggles:
	// a scene can have IBL lighting with no visible sky (e.g. interior with
	// captured env) or a visible sky without IBL contribution (debug view).
	// Gating IBL on skyboxEnabled here used to force both on or both off,
	// which made it impossible to A/B-test the env's contribution to lighting.
	const bool irradianceEnabled =
		target.irradianceEnabled &&
		environmentTexture != nullptr;

	// Both the SKYBOX and ENVIRONMENT layouts use the same binding order:
	//   0 = uniform vec4 environmentParams
	//   1 = sampler
	//   2 = texture
	// so the same override map works for either layout.
	std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> resourceOverrides;
	resourceOverrides.emplace(
		std::make_tuple(0u, 1u),
		webgpu::BindGroupResource(m_context->samplerFactory().getDefaultSampler())
	);
	resourceOverrides.emplace(
		std::make_tuple(0u, 2u),
		webgpu::BindGroupResource(environmentTexture)
	);

	auto bindGroup = m_context->bindGroupFactory().createBindGroup(
		layoutInfo,
		resourceOverrides,
		nullptr,
		debugLabel
	);
	if (!bindGroup)
	{
		spdlog::warn("Failed to create environment bind group '{}' for camera {}", debugLabel, target.cameraId);
		return nullptr;
	}

	const glm::vec4 environmentParams(
		irradianceEnabled ? 1.0f : 0.0f,
		target.irradianceIntensity,
		target.skyboxEnabled ? 1.0f : 0.0f,
		0.0f
	);
	bindGroup->updateBuffer(0, &environmentParams, sizeof(glm::vec4), 0);

	return bindGroup;
}

void Renderer::updateSceneBindGroup(const RenderTarget &target)
{
	// Shader / layout lookup is deferred to the cache-miss branch — on a
	// cache hit the function returns without touching ShaderRegistry's
	// mutex+hashmap. Per camera per frame saving.

	auto sceneLightBuffer = m_context->sceneLightBuffer();
	if (!sceneLightBuffer || !sceneLightBuffer->getBufferWrapped()) return;
	auto lightsBuffer = sceneLightBuffer->getBufferWrapped();

	if (!m_shadowPass) return;
	auto shadowSampler     = m_shadowPass->getShadowSampler();
	auto shadow2DArray     = m_shadowPass->getShadow2DArray();
	auto shadowCubeArray   = m_shadowPass->getShadowCubeArray();
	auto shadowUniformBuf  = m_shadowPass->getShadowUniformBuffer();

	// Environment texture follows the same resolution logic as buildEnvironmentBindGroup.
	std::shared_ptr<webgpu::WebGPUTexture> environmentTexture = m_defaultEnvironmentTexture;
	if (target.environmentTexture.has_value() && target.environmentTexture->valid())
	{
		webgpu::WebGPUTextureOptions options{};
		options.colorSpace = ColorSpace::Linear;
		auto texture = m_context->textureFactory().createFromHandle(target.environmentTexture.value(), options);
		if (texture) environmentTexture = texture;
	}

	// Refresh the prefiltered env mip chain if the source texture changed
	// since the last bake. Identity check by raw WGPUTexture handle —
	// shared_ptr swaps under us when the factory rebuilds (after hot reload
	// / cache eviction) but the handle is stable per backing GPU resource.
	prefilterEnvironment(environmentTexture);

	// Same skybox/IBL decoupling as buildEnvironmentBindGroup above.
	const bool irradianceEnabled =
		target.irradianceEnabled &&
		environmentTexture != nullptr;

	// Env uniforms buffer: cached per camera so the bind group's resource
	// identity stays stable across frames. Writes go directly to the wgpu
	// queue, sidestepping any m_buffers indexing ambiguity inside the bind
	// group.
	auto &envBuffer = m_sceneEnvironmentBuffers[target.cameraId];
	if (!envBuffer)
	{
		envBuffer = m_context->bufferFactory().createUniformBuffer(
			"Scene.EnvironmentParams",
			5,
			sizeof(engine::rendering::EnvironmentUniforms)
		);
	}
	if (!envBuffer) return;

	engine::rendering::EnvironmentUniforms environment;
	environment.params = glm::vec4(
		irradianceEnabled ? 1.0f : 0.0f,
		target.irradianceIntensity,
		target.skyboxEnabled ? 1.0f : 0.0f,
		0.0f
	);
	envBuffer->write(&environment, sizeof(environment));

	auto clusterManager = m_context->clusterManager();
	std::shared_ptr<webgpu::WebGPUBuffer> clusterGrid;
	std::shared_ptr<webgpu::WebGPUBuffer> clusterIndices;
	if (clusterManager)
	{
		clusterGrid    = clusterManager->getClusterGridBuffer();
		clusterIndices = clusterManager->getClusterIndicesBuffer();
	}
	if (!clusterGrid || !clusterIndices)
	{
		spdlog::warn("Scene bind group: cluster buffers unavailable, skipping");
		return;
	}

	auto defaultSampler = m_context->samplerFactory().getDefaultSampler();

	// IBL pair: BRDF integration LUT + GGX-prefiltered env mip chain.
	// Falls back to a 1x1 black texture when either bake is unavailable
	// (early init, scene load gap) so the bind group still validates;
	// the shader treats the missing data as a zero contribution.
	auto brdfLutTex = m_brdfLut.getTexture();
	if (!brdfLutTex) brdfLutTex = m_defaultEnvironmentTexture;

	auto prefilteredEnvTex = m_prefilteredEnv.getTexture();
	if (!prefilteredEnvTex) prefilteredEnvTex = environmentTexture; // fall back to raw env

	// Diffuse irradiance map (cosine-weighted Lambertian convolution).
	// Falls back to the raw env texture when the bake hasn't finished —
	// shaders sample it the same way; the raw env just looks less correct.
	auto irradianceTex = m_irradianceMap.getTexture();
	if (!irradianceTex) irradianceTex = environmentTexture;

	// Identity signature of every resource we're about to bind. Order MUST
	// match the override-insertion order below so the cached signature
	// compares byte-for-byte across frames. If anything moves (lights
	// buffer reallocates, env texture swaps, shadow atlas resizes, IBL
	// bakes finish), the signature changes and we rebuild; otherwise we
	// reuse last frame's bind group — no `device.createBindGroup` call
	// at all on the steady-state per-frame path.
	engine::rendering::cache::BindGroupSignature signature;
	signature.add(lightsBuffer);
	signature.add(shadowSampler);
	signature.add(shadow2DArray);
	signature.add(shadowCubeArray);
	signature.add(shadowUniformBuf);
	signature.add(envBuffer);
	signature.add(defaultSampler);
	signature.add(environmentTexture);
	signature.add(clusterGrid);
	signature.add(clusterIndices);
	signature.add(brdfLutTex);
	signature.add(prefilteredEnvTex);
	signature.add(irradianceTex);

	auto &cachedSig = m_sceneBindGroupSignatures[target.cameraId];
	auto  existing  = m_sceneBindGroups.find(target.cameraId);
	if (existing != m_sceneBindGroups.end() && existing->second && cachedSig == signature)
	{
		// Cache hit: every constituent has the same identity as last
		// time, so the bind group is still valid. Skip the rebuild.
		CacheStats::sceneBindGroupHits.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	CacheStats::sceneBindGroupRebuilds.fetch_add(1, std::memory_order_relaxed);

	// Cache miss: shader lookup happens here, not at the top, so the fast
	// path can skip it entirely.
	auto pbrShader = m_context->shaderRegistry().getShader(shader::defaults::PBR);
	if (!pbrShader) return;
	auto sceneLayout = pbrShader->getBindGroupLayout(bindgroup::defaults::SCENE);
	if (!sceneLayout) return;

	std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> overrides;
	overrides.emplace(std::make_tuple(1u, 0u), webgpu::BindGroupResource(lightsBuffer));
	overrides.emplace(std::make_tuple(1u, 1u), webgpu::BindGroupResource(shadowSampler));
	overrides.emplace(std::make_tuple(1u, 2u), webgpu::BindGroupResource(shadow2DArray));
	overrides.emplace(std::make_tuple(1u, 3u), webgpu::BindGroupResource(shadowCubeArray));
	overrides.emplace(std::make_tuple(1u, 4u), webgpu::BindGroupResource(shadowUniformBuf));
	overrides.emplace(std::make_tuple(1u, 5u), webgpu::BindGroupResource(envBuffer));
	overrides.emplace(std::make_tuple(1u, 6u), webgpu::BindGroupResource(defaultSampler));
	overrides.emplace(std::make_tuple(1u, 7u), webgpu::BindGroupResource(environmentTexture));
	overrides.emplace(std::make_tuple(1u, 8u), webgpu::BindGroupResource(clusterGrid));
	overrides.emplace(std::make_tuple(1u, 9u), webgpu::BindGroupResource(clusterIndices));
	overrides.emplace(std::make_tuple(1u, 10u), webgpu::BindGroupResource(brdfLutTex));
	overrides.emplace(std::make_tuple(1u, 11u), webgpu::BindGroupResource(prefilteredEnvTex));
	overrides.emplace(std::make_tuple(1u, 12u), webgpu::BindGroupResource(irradianceTex));

	auto sceneBindGroup = m_context->bindGroupFactory().createBindGroup(
		sceneLayout, overrides, nullptr, "Scene.BindGroup"
	);
	if (!sceneBindGroup)
	{
		spdlog::warn("Failed to create Scene bind group for camera {}", target.cameraId);
		return;
	}

	m_sceneBindGroups[target.cameraId]           = std::move(sceneBindGroup);
	m_sceneBindGroupSignatures[target.cameraId]  = std::move(signature);
}

void Renderer::updateSkyboxBindGroup(const RenderTarget &target)
{
	// Resolve constituents up front so we can fingerprint them before
	// deciding whether to rebuild. Mirrors the resolution logic inside
	// buildEnvironmentBindGroup. The shader lookup is deferred until the
	// signature-mismatch branch — on cache hit (the common case) we never
	// touch ShaderRegistry's mutex+hashmap.
	std::shared_ptr<webgpu::WebGPUTexture> environmentTexture = m_defaultEnvironmentTexture;
	if (target.environmentTexture.has_value() && target.environmentTexture->valid())
	{
		webgpu::WebGPUTextureOptions options{};
		options.colorSpace = ColorSpace::Linear;
		auto texture = m_context->textureFactory().createFromHandle(target.environmentTexture.value(), options);
		if (texture) environmentTexture = texture;
	}
	auto sampler = m_context->samplerFactory().getDefaultSampler();

	engine::rendering::cache::BindGroupSignature signature;
	signature.add(sampler);
	signature.add(environmentTexture);

	auto &cachedSig = m_skyboxBindGroupSignatures[target.cameraId];
	auto  existing  = m_skyboxBindGroups.find(target.cameraId);
	if (existing != m_skyboxBindGroups.end() && existing->second && cachedSig == signature)
	{
		// Constituents unchanged — refresh env-params (cheap vec4 write,
		// captures user IBL-intensity / skybox-toggle changes) on the
		// cached bind group and skip the rebuild.
		const glm::vec4 environmentParams(
			(target.irradianceEnabled && environmentTexture) ? 1.0f : 0.0f,
			target.irradianceIntensity,
			target.skyboxEnabled ? 1.0f : 0.0f,
			0.0f
		);
		existing->second->updateBuffer(0, &environmentParams, sizeof(glm::vec4), 0);
		CacheStats::skyboxBindGroupHits.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	CacheStats::skyboxBindGroupRebuilds.fetch_add(1, std::memory_order_relaxed);

	// Cache miss: shader lookup is only paid here, not on the fast path.
	auto skyboxShader = m_context->shaderRegistry().getShader(shader::defaults::SKYBOX);
	if (!skyboxShader) return;

	m_skyboxBindGroups[target.cameraId] = buildEnvironmentBindGroup(
		skyboxShader->getBindGroupLayout(bindgroup::defaults::SKYBOX),
		target,
		"Skybox.BindGroup"
	);
	m_skyboxBindGroupSignatures[target.cameraId] = std::move(signature);
}

std::shared_ptr<webgpu::WebGPUTexture> Renderer::updateRenderTexture(
	uint32_t renderTargetId,
	std::shared_ptr<webgpu::WebGPUTexture> &gpuTexture,
	const std::optional<Texture::Handle> &cpuTarget,
	const math::Rect &viewport,
	wgpu::TextureFormat format,
	wgpu::TextureUsage /* usageFlags */
)
{
	// Render targets are textures we render to (instead of directly to screen).
	// Benefits: post-processing, multi-camera rendering, render-to-texture effects

	if (!m_surfaceTexture)
	{
		spdlog::error("Cannot update render texture: surface texture not acquired");
		return nullptr;
	}

	// OPTION 1: Use CPU-provided texture (for render-to-texture effects)
	// The CPU has created a specific texture we should render into
	if (cpuTarget.has_value() && cpuTarget->valid())
	{
		if (auto textureOpt = cpuTarget->get())
		{
			uint32_t targetWidth = textureOpt.value()->getWidth();
			uint32_t targetHeight = textureOpt.value()->getHeight();
			if (textureOpt.value()->isResizeable())
			{
				// Resize texture if it's marked as auto-scaled (e.g., render targets that should match viewport size)
				targetWidth = static_cast<uint32_t>(m_surfaceTexture->getWidth() * viewport.width());
				targetHeight = static_cast<uint32_t>(m_surfaceTexture->getHeight() * viewport.height());
				targetWidth = std::max(1u, targetWidth);   // Ensure minimum size of 1x1
				targetHeight = std::max(1u, targetHeight); // Ensure minimum size of 1x1
				textureOpt.value()->resize(targetWidth, targetHeight);
			}

			// Recreate GPU texture if dimensions or format changed
			if (!gpuTexture || !gpuTexture->matches(targetWidth, targetHeight, format))
			{
				webgpu::WebGPUTextureOptions options{};
				options.generateMipmaps = false;
				gpuTexture = m_context->textureFactory().createRenderTarget(
					renderTargetId,
					targetWidth,
					targetHeight,
					format
				);
			}
			return gpuTexture;
		}
	}

	// OPTION 2: Use viewport-relative dimensions (for split-screen, picture-in-picture)
	// Calculate texture size based on viewport percentage of surface
	uint32_t targetWidth = static_cast<uint32_t>(m_surfaceTexture->getWidth() * viewport.width());
	uint32_t targetHeight = static_cast<uint32_t>(m_surfaceTexture->getHeight() * viewport.height());

	if (targetWidth == 0 || targetHeight == 0)
	{
		spdlog::warn("Invalid viewport dimensions: {}x{}", targetWidth, targetHeight);
		return nullptr;
	}

	// Recreate render target if it doesn't exist or dimensions changed (e.g., window resize)
	if (!gpuTexture || !gpuTexture->matches(targetWidth, targetHeight, format))
	{
		gpuTexture = m_context->textureFactory().createRenderTarget(
			renderTargetId,
			targetWidth,
			targetHeight,
			format
		);
	}

	return gpuTexture;
}
void Renderer::renderToTexture(
	const RenderCollector &collector,
	const DebugRenderCollector &debugCollector,
	RenderTarget &renderTarget,
	const std::vector<BindGroupDataProvider> &customBindGroupProviders
)
{
	auto renderTargetId = renderTarget.cameraId;
	spdlog::debug(
		"renderToTexture: cameraId={}, renderItems={}, lights={}",
		renderTargetId,
		collector.getRenderItems().size(),
		collector.getLights().size()
	);

	// ========================================
	// STEP 1: Prepare Render Target Texture
	// ========================================
	// Update or create the texture we'll render this camera's view into.
	// Uses HDR format (RGBA16Float) for better color precision in lighting calculations.
	{
		FrameProfiler::Scope s(m_profiler, "Frame.UpdateRenderTextures");
		renderTarget.gpuTexture = updateRenderTexture(
			renderTargetId,
			renderTarget.gpuTexture,
			renderTarget.cpuTarget,
			renderTarget.viewport,
			wgpu::TextureFormat::RGBA16Float,
			static_cast<WGPUTextureUsage>(
				wgpu::TextureUsage::RenderAttachment | // Can render to it
				wgpu::TextureUsage::TextureBinding |   // Can sample from it in shaders
				wgpu::TextureUsage::CopySrc			   // Can copy data from it (for readback)
			)
		);
	}

	if (!renderTarget.gpuTexture)
	{
		spdlog::error("Failed to create/update render target texture");
		return;
	}

	// Output texture by default is the same as input (rendering directly into it), but can be changed to a different texture for post-processing effects.
	auto renderFromTexture = renderTarget.gpuTexture;
	// This is the texture that MeshPass (as well as DebugPass) will render into, and that PostProcessingPass will read from.
	auto renderToTexture = renderTarget.gpuTexture;

	// ========================================
	// STEP 2: Prepare Depth Buffer and Post-Processing Texture
	// ========================================
	// Depth buffer stores per-pixel depth values for depth testing.
	// Essential for correct rendering of overlapping geometry.
	if (!m_depthBuffers[renderTargetId])
	{
		m_depthBuffers[renderTargetId] = m_context->depthTextureFactory().createDefault(
			renderFromTexture->getWidth(),
			renderFromTexture->getHeight(),
			wgpu::TextureFormat::Depth32Float
		);
	} else if(m_depthBuffers[renderTargetId]->getWidth() != renderFromTexture->getWidth() ||
			  m_depthBuffers[renderTargetId]->getHeight() != renderFromTexture->getHeight())
	{
		m_depthBuffers[renderTargetId]->resize(*m_context, renderFromTexture->getWidth(), renderFromTexture->getHeight());
	}
	// Tutorial 04 - Step 10: Prepare post-processing texture
	// Post-processing texture is an intermediate render target for effects like bloom, tone mapping, etc.
	// Use negative ID to differentiate post-process textures from main render targets

	// ========================================
	// STEP 3: Frustum Culling
	// ========================================
	// Fingerprint the inputs that determine the visibility set: the
	// view-projection matrix (camera moves) and the collector's item count
	// (objects added / removed). When both match the last frame's
	// fingerprint, the visibility set is unchanged and we reuse the cached
	// indices — skipping the O(N items) cull and the 6 plane-extraction
	// from the matrix. For typical orbit-camera demos the matrix changes
	// every frame and we always miss, but idle / paused / scripted-view
	// frames pay zero CPU here. Edge: object-moves-while-camera-static-
	// and-count-stable reuses last frame's set for one frame; for typical
	// scene velocities the visibility slip is invisible.
	std::vector<size_t> visibleIndices;
	auto                &cullCache    = m_cullCaches[renderTargetId];
	const std::size_t    currentCount = collector.getRenderItems().size();
	if (cullCache.valid
		&& cullCache.lastItemsCount == currentCount
		&& cullCache.lastViewProjection == renderTarget.viewProjectionMatrix)
	{
		visibleIndices = cullCache.visibleIndices;
		CacheStats::frustumCullsSkipped.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		auto cameraFrustum = engine::math::Frustum::fromViewProjection(renderTarget.viewProjectionMatrix);
		{
			FrameProfiler::Scope s(m_profiler, "Frame.FrustumCull");
			visibleIndices = collector.extractVisible(cameraFrustum);
		}
		cullCache.lastViewProjection = renderTarget.viewProjectionMatrix;
		cullCache.lastItemsCount     = currentCount;
		cullCache.visibleIndices     = visibleIndices;
		cullCache.valid              = true;
		CacheStats::frustumCullsExecuted.fetch_add(1, std::memory_order_relaxed);
	}

	spdlog::debug("Frustum culling: {} visible of {} total items", visibleIndices.size(), collector.getRenderItems().size());

	// ========================================
	// STEP 3.5: Process Custom Bind Group Data from Scene
	// ========================================
	// Process custom bind group providers collected by Scene during preRender().
	// Scene traverses all enabled nodes and calls their preRender() methods.
	if (!customBindGroupProviders.empty())
	{
		m_frameCache.processBindGroupProviders(m_context, customBindGroupProviders);
		spdlog::debug("Processed {} custom bind group providers", customBindGroupProviders.size());
	}

	// ========================================
	// STEP 4: Prepare GPU Resources
	// ========================================
	// Upload/update GPU buffers for visible meshes and materials.
	// Only creates GPU resources for objects that passed frustum culling.
	{
		FrameProfiler::Scope s(m_profiler, "Frame.PrepareGPU");
		m_frameCache.prepareGPUResources(m_context, collector, visibleIndices);
	}

	// ========================================
	// STEP 5: Deferred passes via per-camera RenderGraph
	// ========================================
	// The graph drives GBuffer → ClusterCompute → Composition → Skybox →
	// ForwardTransparency → Debug in dependency order. Each pass's lambda
	// (defined in buildPerCameraGraph) reads m_currentCamera for its
	// per-camera state — we set those fields HERE, before execute().
	// Built lazily on first frame because initialize() runs before some
	// of the pass plumbing (shader resolution, etc.) is ready.
	if (!m_perCameraGraphReady)
	{
		buildPerCameraGraph();
	}

	if (m_perCameraGraphReady)
	{
		m_currentCamera.renderTarget   = &renderTarget;
		m_currentCamera.renderTexture  = renderToTexture;
		m_currentCamera.visibleIndices = visibleIndices;
		m_currentCamera.debugCollector = &debugCollector;

		engine::rendergraph::RenderContext ctx;
		m_perCameraGraph.execute(ctx);

		m_currentCamera = {};
	}

	// ========================================
	// STEP 7: Post-Processing Pass
	// ========================================
	// Tutorial 04 - Step 11: Apply vignette effect
	// Texture swapping: MeshPass/DebugPass output → input for post-processing
	// Output: Post-processed image (stored in m_postProcessTextures for CompositePass)

	// ========================================
	// STEP 7: CPU Readback (Optional)
	// ========================================
	// If the application requested to read pixels back to CPU (e.g., for screenshots),
	// initiate asynchronous GPU-to-CPU transfer.
	if (renderTarget.cpuTarget.has_value() && renderTarget.cpuTarget->valid())
	{
		if (auto textureOpt = renderTarget.cpuTarget->get())
		{
			auto &tex = textureOpt.value();

			// Phase 1 ÔÇö initiate readback if requested and none in flight
			if (tex->isReadbackRequested() && !renderToTexture->isReadbackPending())
			{
				renderToTexture->beginReadback(*m_context);
			}

			// Phase 2 ÔÇö poll every frame until GPU callback fires
			if (renderToTexture->isReadbackPending())
			{
				if (renderToTexture->pollReadback(*m_context, tex))
					tex->resolveReadback();
			}
		}
	}
	// ========================================
	// STEP 8: Store Final Texture for Compositing
	// ========================================
	m_frameCache.finalTextures[renderTargetId] = renderToTexture; // Store post-processed texture for compositing pass
}

void Renderer::compositeTexturesToSurface(
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	// ========================================
	// Compositing Phase
	// ========================================
	// Composite all camera render targets into the final surface texture.
	// For single camera: simple copy/blit to surface
	// For multiple cameras: arrange viewports (split-screen, picture-in-picture)
	auto compositePassContext = m_context->renderPassFactory().create(m_surfaceTexture);
	m_compositePass->setRenderPassContext(compositePassContext);
	if (m_compositePass->isEnabled())
	{
		m_compositePass->render(m_frameCache);
	}

	// ========================================
	// UI Rendering Phase (Optional)
	// ========================================
	// UI/ImGui rendering happens in a separate pass after all 3D rendering is complete.
	// This ensures UI always renders on top of the scene.
	if (uiCallback)
	{
		auto uiEncoder = m_context->createCommandEncoder("UI Command Encoder");

		// Create render pass that renders over existing surface (no clear)
		auto uiPassContext = m_context->renderPassFactory().create(
			m_surfaceTexture,
			nullptr,		 // No depth buffer needed for 2D UI
			ClearFlags::None // Don't clear - render on top
		);

		wgpu::RenderPassEncoder uiRenderPass = uiPassContext->begin(uiEncoder);
		uiCallback(uiRenderPass); // Application draws UI (e.g., ImGui)
		uiPassContext->end(uiRenderPass);

		m_context->submitCommandEncoder(uiEncoder, "UI Commands");
	}
}

std::vector<RenderPass *> Renderer::getAllPasses()
{
	// Order matches the actual render-time invocation order. Adding a new
	// pass? Insert it where it runs so the UI's vertical list mirrors the
	// pipeline.
	std::vector<RenderPass *> out;
	out.reserve(8);
	if (m_shadowPass)       out.push_back(m_shadowPass.get());
	if (m_gBufferPass)      out.push_back(m_gBufferPass.get());
	if (m_compositionPass)  out.push_back(m_compositionPass.get());
	if (m_skyboxPass)       out.push_back(m_skyboxPass.get());
	if (m_transparencyPass) out.push_back(m_transparencyPass.get());
	if (m_debugPass)        out.push_back(m_debugPass.get());
	if (m_meshPass)         out.push_back(m_meshPass.get());
	if (m_compositePass)    out.push_back(m_compositePass.get());
	return out;
}

void Renderer::onResize(uint32_t width, uint32_t height)
{
	for (auto &[id, target] : m_renderTargets)
	{
		if (target.gpuTexture && !target.cpuTarget.has_value())
			target.gpuTexture.reset();
		auto viewport = target.viewport;
		auto viewPortWidth = static_cast<uint32_t>(width * viewport.width());
		auto viewPortHeight = static_cast<uint32_t>(height * viewport.height());
		auto depthBuffer = m_depthBuffers[id];
		if (depthBuffer)
			depthBuffer->resize(*m_context, viewPortWidth, viewPortHeight);
		// Tutorial 04 - Step 12: Handle window resize
		// When the window resizes, we need to resize all render targets and depth buffers accordingly
	}

	if (m_meshPass)
		m_meshPass->cleanup();

	if (m_compositePass)
		m_compositePass->cleanup();

	if (m_shadowPass)
		m_shadowPass->cleanup();

	if (m_skyboxPass)
		m_skyboxPass->cleanup();

	// The G-buffer resizes per-camera inside renderToTexture, but its cached
	// render-pass context and the composition pass's cached pipeline + G-buffer
	// bind group need to be invalidated so they pick up the new attachments.
	if (m_gBufferPass)
		m_gBufferPass->cleanup();

	if (m_compositionPass)
		m_compositionPass->cleanup();

	spdlog::info("Renderer resized to {}x{}", width, height);
}

} // namespace engine::rendering
