#include "engine/rendering/Renderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/BindGroupDataProvider.h"
#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/CompositePass.h"
#include "engine/rendering/DebugPass.h"
#include "engine/rendering/DebugRenderCollector.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/MeshPass.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/PostProcessingPass.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/RenderPassManager.h"
#include "engine/rendering/RenderTarget.h"
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/ShadowPass.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/RenderNode.h"

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

	spdlog::info("Renderer initialized successfully");
	return true;
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
	// === PHASE 1: Frame Initialization ===
	// Acquire swap chain texture and reset GPU resource cache
	startFrame();
	if (renderTargets.empty())
	{
		spdlog::warn("renderFrame called with no render targets");
		return false;
	}

	// === PHASE 2: Prepare Render Targets ===
	// Deduplicate render targets (one per camera) and prepare per-camera frame uniforms.
	// Frame uniforms contain: view matrix, projection matrix, camera position, time, etc.
	std::unordered_map<uint64_t, RenderTarget> uniqueRenderTargets;
	for (const auto &target : renderTargets)
	{
		if (uniqueRenderTargets.count(target.cameraId))
		{
			spdlog::warn("Duplicate render target for cameraId {} detected; skipping", target.cameraId);
			continue;
		}
		uniqueRenderTargets[target.cameraId] = target;

		// Create/update bind group with camera-specific uniforms (view, projection)
		updateFrameBindGroup(target, time);
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
	m_renderTargets = m_frameCache.renderTargets; // ToDO: remove m_renderTargets member and use m_frameCache everywhere

	// Extract light data and determine which lights need shadow maps
	auto [lightUniforms, shadowRequests] = renderCollector.extractLightsAndShadows(
		constants::MAX_SHADOW_MAPS_2D,
		constants::MAX_SHADOW_MAPS_CUBE
	);
	m_frameCache.lightUniforms = std::move(lightUniforms);
	m_frameCache.shadowRequests = std::move(shadowRequests);

	// === PHASE 4: Render Each Camera View ===
	// Multi-camera rendering: each camera gets its own shadow maps and scene render
	m_shadowPass->setRenderCollector(&renderCollector);
	for (auto &[cameraId, target] : m_frameCache.renderTargets)
	{
		// Render shadow maps from the perspective of lights visible to this camera
		m_shadowPass->setCameraId(target.cameraId);
		m_shadowPass->render(m_frameCache);

		// Render scene from camera's perspective (with shadows applied)
		renderToTexture(renderCollector, debugRenderCollector, target, customBindGroupProviders);
	}

	// === PHASE 5: Composite & Present ===
	// Combine all camera render targets into final surface texture, then present to screen
	compositeTexturesToSurface(uiCallback);
	m_context->getSurface().present();
	m_surfaceTexture.reset();

	// === PHASE 6: Post-Frame Cleanup ===
	// Hot-reload shaders if changed, clear frame cache for next frame
	m_context->pipelineManager().processPendingReloads();
	m_frameCache.clear();

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

			// Recreate GPU texture if dimensions or format changed
			if (!gpuTexture || !gpuTexture->matches(targetWidth, targetHeight, format))
			{
				gpuTexture = m_context->textureFactory().createFromHandle(cpuTarget.value());
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
	std::vector<size_t> visibleIndices = collector.extractVisible(cameraFrustum);

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
	m_frameCache.prepareGPUResources(m_context, collector, visibleIndices);

	// ========================================
	// STEP 5: Mesh Rendering Pass
	// ========================================
	// Render all visible based on material and mesh configurations.
	auto meshPassContext = m_context->renderPassFactory().create(
		renderToTexture,				// Color attachment
		m_depthBuffers[renderTargetId], // Depth attachment
		renderTarget.clearFlags,		// Clear color/depth?
		renderTarget.backgroundColor	// Clear color
	);

	m_meshPass->setRenderPassContext(meshPassContext);
	m_meshPass->setCameraId(renderTargetId);
	m_meshPass->setVisibleIndices(visibleIndices);
	m_meshPass->setShadowBindGroup(m_shadowPass->getShadowBindGroup());

	spdlog::debug("Rendering {} GPU mesh items", m_frameCache.gpuRenderItems.size());
	m_meshPass->render(m_frameCache);

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
	m_debugPass->render(m_frameCache);

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
			if (textureOpt.value()->isReadbackRequested())
			{
				auto readbackFuture = renderToTexture->readbackToCPUAsync(
					*m_context,
					textureOpt.value()
				);
				textureOpt.value()->setReadbackFuture(std::move(readbackFuture));
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
		wgpu::CommandEncoderDescriptor encoderDesc{};
		encoderDesc.label = "UI Command Encoder";
		wgpu::CommandEncoder uiEncoder = m_context->getDevice().createCommandEncoder(encoderDesc);

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
	// Tutorial 04 - Step 12: Handle window resize (continued)
	// Post-processing pass also needs to clean up GPU resources so it can recreate them with new sizes.
	// If we don't clean up, we'll have dangling GPU resources with old dimensions that cause rendering issues.

	spdlog::info("Renderer resized to {}x{}", width, height);
}

} // namespace engine::rendering
