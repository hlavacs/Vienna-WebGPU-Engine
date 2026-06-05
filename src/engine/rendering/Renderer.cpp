#include "engine/rendering/Renderer.h"

#include <limits>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "engine/lighting/LightManager.h"
#include "engine/rendering/BindGroupDataProvider.h"
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

	// Build a representative render graph mirroring the per-camera frame
	// flow and log its compiled order. This validates the dependency
	// declarations match the hand-coded sequence we're orchestrating below.
	// When the per-pass orchestration migrates to the graph for real, the
	// execute lambdas just stop being no-ops and the rendering becomes
	// graph-driven without changing the dependency declarations.
	logFrameGraphLayout();

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

namespace
{

/// Logical resource handles the per-camera frame graph wires its passes
/// against. Names are descriptive — they don't bind to wgpu objects yet;
/// that's the next migration step.
struct FrameGraphResources
{
	engine::rendergraph::ResourceHandle shadowMaps;
	engine::rendergraph::ResourceHandle shadowUniform;
	engine::rendergraph::ResourceHandle gbufferColor;
	engine::rendergraph::ResourceHandle gbufferDepth;
	engine::rendergraph::ResourceHandle clusterGrid;
	engine::rendergraph::ResourceHandle clusterIdx;
	engine::rendergraph::ResourceHandle litHdr;
	engine::rendergraph::ResourceHandle backbuffer;
};

FrameGraphResources importFrameGraphResources(engine::rendergraph::RenderGraph &g)
{
	using namespace engine::rendergraph;
	FrameGraphResources r{};
	r.shadowMaps    = g.addImported("ShadowMaps",     ResourceType::DepthTexture);
	r.shadowUniform = g.addImported("ShadowUniform",  ResourceType::Buffer);
	r.gbufferColor  = g.addImported("GBuffer.Color",  ResourceType::ColorTexture);
	r.gbufferDepth  = g.addImported("GBuffer.Depth",  ResourceType::DepthTexture);
	r.clusterGrid   = g.addImported("ClusterGrid",    ResourceType::Buffer);
	r.clusterIdx    = g.addImported("ClusterIndices", ResourceType::Buffer);
	r.litHdr        = g.addImported("LitHDR",         ResourceType::ColorTexture);
	r.backbuffer    = g.addImported("Backbuffer",     ResourceType::ColorTexture);
	return r;
}

void registerFrameGraphPasses(
	engine::rendergraph::RenderGraph &g,
	const FrameGraphResources &r)
{
	using namespace engine::rendergraph;
	// Execute lambdas are no-ops here — the integration step is replacing
	// the renderFrame per-pass blocks with `graph.execute(ctx)` and letting
	// these lambdas run the existing pass.render() calls. Skybox /
	// ForwardTransparency / Debug all LOAD (not clear) litHdr — they sample
	// the existing lit pixels and additively modify, so they must declare a
	// read in addition to the write. Without the read the graph has no edge
	// from Composition to them and would be free to reorder.
	auto noop = [](RenderContext &) {};

	g.addPass(std::make_unique<FunctionPass>("Shadow",
		std::vector<ResourceHandle>{},
		std::vector<ResourceHandle>{r.shadowMaps, r.shadowUniform}, noop));

	g.addPass(std::make_unique<FunctionPass>("GBuffer",
		std::vector<ResourceHandle>{},
		std::vector<ResourceHandle>{r.gbufferColor, r.gbufferDepth}, noop));

	g.addPass(std::make_unique<FunctionPass>("ClusterCompute",
		std::vector<ResourceHandle>{},
		std::vector<ResourceHandle>{r.clusterGrid, r.clusterIdx}, noop));

	g.addPass(std::make_unique<FunctionPass>("Composition",
		std::vector<ResourceHandle>{r.gbufferColor, r.gbufferDepth, r.shadowMaps, r.shadowUniform, r.clusterGrid, r.clusterIdx},
		std::vector<ResourceHandle>{r.litHdr}, noop));

	g.addPass(std::make_unique<FunctionPass>("Skybox",
		std::vector<ResourceHandle>{r.gbufferDepth, r.litHdr},
		std::vector<ResourceHandle>{r.litHdr}, noop));

	g.addPass(std::make_unique<FunctionPass>("ForwardTransparency",
		std::vector<ResourceHandle>{r.gbufferDepth, r.litHdr},
		std::vector<ResourceHandle>{r.litHdr}, noop));

	g.addPass(std::make_unique<FunctionPass>("Debug",
		std::vector<ResourceHandle>{r.litHdr},
		std::vector<ResourceHandle>{r.litHdr}, noop));

	g.addPass(std::make_unique<FunctionPass>("Composite",
		std::vector<ResourceHandle>{r.litHdr},
		std::vector<ResourceHandle>{r.backbuffer}, noop));
}

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

void Renderer::logFrameGraphLayout()
{
	engine::rendergraph::RenderGraph g;
	const auto resources = importFrameGraphResources(g);
	registerFrameGraphPasses(g, resources);

	auto result = g.compile();
	if (!result.success)
	{
		spdlog::error("Frame render graph compile FAILED: {}", result.error);
		return;
	}

	spdlog::info("Frame render graph compiled: {} passes, {} resources",
		g.passCount(), g.resourceCount());
	spdlog::info("Order: {}", joinPassNames(g.compiledOrder()));
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

	// === PHASE 4: Render Each Camera View ===
	// Multi-camera rendering: each camera gets its own shadow maps and scene render
	m_shadowPass->setRenderCollector(&renderCollector);
	for (auto &[cameraId, target] : m_frameCache.renderTargets)
	{
		// Render shadow maps from the perspective of lights visible to this camera
		m_shadowPass->setCameraId(target.cameraId);
		{
			FrameProfiler::Scope s(m_profiler, "Pass.Shadow");
			m_shadowPass->render(m_frameCache);
		}

		// Render scene from camera's perspective (with shadows applied)
		renderToTexture(renderCollector, debugRenderCollector, target, customBindGroupProviders);
	}

	// === PHASE 5: Composite & Present ===
	// Combine all camera render targets into final surface texture, then present to screen
	{
		FrameProfiler::Scope s(m_profiler, "Pass.Composite+UI");
		compositeTexturesToSurface(uiCallback);
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
	// Hot-reload shaders if changed, clear frame cache for next frame.
	m_context->pipelineManager().processPendingReloads();
	m_frameCache.clear();

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

	m_frameCache.gpuRenderItems.clear(); // Clear GPU render items at the start of each frame
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
	m_frameCache.frameBindGroupCache[target.cameraId]->updateBuffer(
		0,					   // binding index
		&frameUniforms,		   // source data
		sizeof(FrameUniforms), // data size
		0,					   // buffer offset
		m_context->getQueue()  // GPU queue for transfer
	);
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

	const bool irradianceEnabled =
		target.skyboxEnabled &&
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
	bindGroup->updateBuffer(0, &environmentParams, sizeof(glm::vec4), 0, m_context->getQueue());

	return bindGroup;
}

void Renderer::updateSceneBindGroup(const RenderTarget &target)
{
	auto pbrShader = m_context->shaderRegistry().getShader(shader::defaults::PBR);
	if (!pbrShader) return;
	auto sceneLayout = pbrShader->getBindGroupLayout(bindgroup::defaults::SCENE);
	if (!sceneLayout) return;

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

	const bool irradianceEnabled =
		target.skyboxEnabled &&
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
	m_context->getQueue().writeBuffer(envBuffer->getBuffer(), 0, &environment, sizeof(environment));

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

	std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> overrides;
	overrides.emplace(std::make_tuple(1u, 0u), webgpu::BindGroupResource(lightsBuffer));
	overrides.emplace(std::make_tuple(1u, 1u), webgpu::BindGroupResource(shadowSampler));
	overrides.emplace(std::make_tuple(1u, 2u), webgpu::BindGroupResource(shadow2DArray));
	overrides.emplace(std::make_tuple(1u, 3u), webgpu::BindGroupResource(shadowCubeArray));
	overrides.emplace(std::make_tuple(1u, 4u), webgpu::BindGroupResource(shadowUniformBuf));
	overrides.emplace(std::make_tuple(1u, 5u), webgpu::BindGroupResource(envBuffer));
	overrides.emplace(std::make_tuple(1u, 6u),
		webgpu::BindGroupResource(m_context->samplerFactory().getDefaultSampler()));
	overrides.emplace(std::make_tuple(1u, 7u), webgpu::BindGroupResource(environmentTexture));
	overrides.emplace(std::make_tuple(1u, 8u), webgpu::BindGroupResource(clusterGrid));
	overrides.emplace(std::make_tuple(1u, 9u), webgpu::BindGroupResource(clusterIndices));

	// IBL pair: BRDF integration LUT + GGX-prefiltered env mip chain.
	// Falls back to a 1x1 black texture when either bake is unavailable
	// (early init, scene load gap) so the bind group still validates;
	// the shader treats the missing data as a zero contribution.
	auto brdfLutTex = m_brdfLut.getTexture();
	if (!brdfLutTex) brdfLutTex = m_defaultEnvironmentTexture;
	overrides.emplace(std::make_tuple(1u, 10u), webgpu::BindGroupResource(brdfLutTex));

	auto prefilteredEnvTex = m_prefilteredEnv.getTexture();
	if (!prefilteredEnvTex) prefilteredEnvTex = environmentTexture; // fall back to raw env
	overrides.emplace(std::make_tuple(1u, 11u), webgpu::BindGroupResource(prefilteredEnvTex));

	// Diffuse irradiance map (cosine-weighted Lambertian convolution).
	// Falls back to the raw env texture when the bake hasn't finished —
	// shaders sample it the same way; the raw env just looks less correct.
	auto irradianceTex = m_irradianceMap.getTexture();
	if (!irradianceTex) irradianceTex = environmentTexture;
	overrides.emplace(std::make_tuple(1u, 12u), webgpu::BindGroupResource(irradianceTex));

	auto sceneBindGroup = m_context->bindGroupFactory().createBindGroup(
		sceneLayout, overrides, nullptr, "Scene.BindGroup"
	);
	if (!sceneBindGroup)
	{
		spdlog::warn("Failed to create Scene bind group for camera {}", target.cameraId);
		return;
	}

	m_sceneBindGroups[target.cameraId] = std::move(sceneBindGroup);
}

void Renderer::updateSkyboxBindGroup(const RenderTarget &target)
{
	auto skyboxShader = m_context->shaderRegistry().getShader(shader::defaults::SKYBOX);
	if (!skyboxShader) return;
	m_skyboxBindGroups[target.cameraId] = buildEnvironmentBindGroup(
		skyboxShader->getBindGroupLayout(bindgroup::defaults::SKYBOX),
		target,
		"Skybox.BindGroup"
	);
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
	// Build camera frustum from view-projection matrix and cull objects outside view.
	// Frustum culling optimization: don't render objects the camera can't see.
	auto cameraFrustum = engine::math::Frustum::fromViewProjection(renderTarget.viewProjectionMatrix);
	std::vector<size_t> visibleIndices;
	{
		FrameProfiler::Scope s(m_profiler, "Frame.FrustumCull");
		visibleIndices = collector.extractVisible(cameraFrustum);
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
	// STEP 5: Deferred Geometry + Composition
	// ========================================
	// GBufferPass owns its attachments and exposes a typed RenderPassContext;
	// the renderer's only job is to (re)size it to match this camera and then
	// hand off the visible items. BLEND materials stay in the gbuffer (handled
	// via alpha-test discard in the g_buffer shader); a separate forward
	// transparency pass for truly translucent geometry is a follow-up.
	auto &gBuffer = m_gBufferPass->getGBuffer();
	m_gBufferPass->resize(renderFromTexture->getWidth(), renderFromTexture->getHeight());
	m_gBufferPass->setCameraId(renderTargetId);
	m_gBufferPass->setVisibleIndices(visibleIndices);
	{
		FrameProfiler::Scope s(m_profiler, "Pass.GBuffer");
		m_gBufferPass->render(m_frameCache);
	}

	// Run the compute clustering pass for this camera. Has to come AFTER the
	// frame bind group + scene light buffer are populated (both happen earlier
	// in renderToTexture's setup) and BEFORE the composition pass reads from
	// the cluster buffers.
	auto sceneLightBuffer = m_context->sceneLightBuffer();
	auto clusterManager = m_context->clusterManager();
	if (sceneLightBuffer && sceneLightBuffer->getBufferWrapped() && clusterManager)
	{
		FrameProfiler::Scope s(m_profiler, "Pass.ClusterCompute");
		if (!clusterManager->assignLights(renderTargetId, m_frameCache, sceneLightBuffer->getLightCount()))
			spdlog::warn("Failed to assign lights to clusters");
	}

	// Build the consolidated Scene bind group for this camera. Composition
	// and ForwardTransparency share it: same layout, same instance.
	{
		FrameProfiler::Scope s(m_profiler, "Frame.SceneBindGroup");
		updateSceneBindGroup(renderTarget);
	}
	auto sceneBindGroup = m_sceneBindGroups[renderTargetId];

	if (!sceneBindGroup)
	{
		spdlog::warn("CompositionPass skipped: scene bind group failed to build");
	}
	else
	{
		// HDR intermediate is the camera's render target texture - cleared once
		// here so the composition pass overwrites every pixel with the lit result.
		auto compositionPassContext = m_context->renderPassFactory().create(
			renderToTexture,
			nullptr,
			ClearFlags::SolidColor,
			renderTarget.backgroundColor
		);

		m_compositePass->setHDREnabled(renderTarget.hdr);
		m_compositionPass->setRenderPassContext(compositionPassContext);
		m_compositionPass->setGBuffer(&gBuffer);
		m_compositionPass->setSceneBindGroup(sceneBindGroup);
		m_compositionPass->setCameraId(renderTargetId);
		{
			FrameProfiler::Scope s(m_profiler, "Pass.Composition");
			m_compositionPass->render(m_frameCache);
		}
	}

	// ========================================
	// STEP 5b: Skybox (background fill)
	// ========================================
	// Drawn AFTER composition so it only fills pixels where the geometry pass
	// left the cleared depth value (1.0). The skybox shader writes clip.xyww
	// (so every fragment lands at depth = 1.0), the pipeline is configured
	// LessEqual + depth-write-off, and the pass loads (not clears) the HDR
	// color target plus the gbuffer depth.
	if (renderTarget.skyboxEnabled)
	{
		updateSkyboxBindGroup(renderTarget);
		auto skyboxBindGroup = m_skyboxBindGroups[renderTargetId];
		if (skyboxBindGroup)
		{
			auto skyboxPassContext = m_context->renderPassFactory().create(
				renderToTexture,
				gBuffer.getDepthTexture(),
				ClearFlags::None, // LoadOp::Load for both color and depth
				renderTarget.backgroundColor
			);
			m_skyboxPass->setRenderPassContext(skyboxPassContext);
			m_skyboxPass->setCameraId(renderTargetId);
			m_skyboxPass->setEnvironmentBindGroup(skyboxBindGroup);
			{
				FrameProfiler::Scope s(m_profiler, "Pass.Skybox");
				m_skyboxPass->render(m_frameCache);
			}
		}
	}

	// ========================================
	// STEP 5c: Forward Transparency
	// ========================================
	// Runs after lit-HDR composition + skybox so the blend destination is final
	// opaque colour. Reads gbuffer depth read-only - transparent surfaces must
	// not stamp depth, or later transparent draws behind them get occluded.
	//
	// Scene bind group already built above for composition; reuse it here.
	if (m_transparencyPass && sceneBindGroup)
	{
		auto transparencyPassContext = m_context->renderPassFactory().create(
			renderToTexture,
			gBuffer.getDepthTexture(),
			ClearFlags::None,
			renderTarget.backgroundColor
		);
		m_transparencyPass->setRenderPassContext(transparencyPassContext);
		m_transparencyPass->setCameraId(renderTargetId);
		m_transparencyPass->setCameraPosition(renderTarget.cameraPosition);
		m_transparencyPass->setVisibleIndices(visibleIndices);
		m_transparencyPass->setSceneBindGroup(sceneBindGroup);
		{
			FrameProfiler::Scope s(m_profiler, "Pass.ForwardTransparency");
			m_transparencyPass->render(m_frameCache);
		}
	}

	// ========================================
	// STEP 6: Debug Rendering Pass
	// ========================================
	// Render debug visualization (wireframes, bounding boxes, gizmos) on top.
	// No depth clearing - renders over the scene.
	auto debugPassContext = m_context->renderPassFactory().create(
		renderToTexture,  // Color attachment (same as main render pass)
		nullptr,		  // No depth attachment (use existing depth)
		ClearFlags::None, // Don't clear anything
		renderTarget.backgroundColor
	);

	m_debugPass->setRenderPassContext(debugPassContext);
	m_debugPass->setCameraId(renderTargetId);
	m_debugPass->setDebugCollector(&debugCollector);
	{
		FrameProfiler::Scope s(m_profiler, "Pass.Debug");
		m_debugPass->render(m_frameCache);
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
	m_compositePass->render(m_frameCache);

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
