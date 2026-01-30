#include "engine/rendering/Renderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/CompositePass.h"
#include "engine/rendering/DebugRenderCollector.h"
#include "engine/rendering/DebugPass.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/MeshPass.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/ObjectUniforms.h"
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

	// Create depth buffer
	m_depthBuffer = m_context->depthTextureFactory().createDefault(
		m_context->surfaceManager().currentConfig().width,
		m_context->surfaceManager().currentConfig().height,
		wgpu::TextureFormat::Depth32Float
	);

	if (!m_depthBuffer)
	{
		spdlog::error("Failed to create depth buffer");
		return false;
	}

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
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	startFrame();
	if (renderTargets.empty())
	{
		spdlog::warn("renderFrame called with no render targets");
		return false;
	}

	std::unordered_map<uint64_t, RenderTarget> uniqueRenderTargets;
	for (const auto &target : renderTargets)
	{
		if (uniqueRenderTargets.find(target.cameraId) != uniqueRenderTargets.end())
		{
			spdlog::warn("Duplicate render target for cameraId {} detected; skipping", target.cameraId);
			continue;
		}
		uniqueRenderTargets[target.cameraId] = target;
		if (m_frameCache.frameBindGroupCache[target.cameraId] == nullptr)
		{
			m_frameCache.frameBindGroupCache[target.cameraId] = m_context->bindGroupFactory().createBindGroup(m_frameBindGroupLayout);
		}
		auto frameUniforms = target.getFrameUniforms(m_frameCache.time);
		m_frameCache.frameBindGroupCache[target.cameraId]->updateBuffer(
			0,
			&frameUniforms,
			sizeof(FrameUniforms),
			0,
			m_context->getQueue()
		);
	}
	m_frameCache.lights = renderCollector.getLights();
	m_frameCache.renderTargets = std::move(uniqueRenderTargets);
	m_frameCache.time = time;

	// Extract lights and shadow requests (camera-independent)
	auto [lightUniforms, shadowRequests] = renderCollector.extractLightsAndShadows(
		constants::MAX_SHADOW_MAPS_2D,
		constants::MAX_SHADOW_MAPS_CUBE
	);
	m_frameCache.lightUniforms = std::move(lightUniforms);
	m_frameCache.shadowRequests = std::move(shadowRequests);

	// Render shadow maps (camera-aware - computes matrices per frame)
	m_shadowPass->setRenderCollector(&renderCollector);

	for (auto &[targetId, target] : m_frameCache.renderTargets)
	{
		m_shadowPass->setCameraId(target.cameraId);
		m_shadowPass->render(m_frameCache);
		renderToTexture(
			renderCollector,
			debugRenderCollector,
			target
		);
	}

	compositeTexturesToSurface(uiCallback);

	m_context->getSurface().present();
	m_surfaceTexture.reset();

	// Process any pending pipeline reloads after frame is complete
	m_context->pipelineManager().processPendingReloads();
	// Clear frame cache after rendering
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

std::shared_ptr<webgpu::WebGPUTexture> Renderer::updateRenderTexture(
	uint32_t renderTargetId,
	std::shared_ptr<webgpu::WebGPUTexture> &gpuTexture,
	const std::optional<Texture::Handle> &cpuTarget,
	const glm::vec4 &viewport,
	wgpu::TextureFormat format,
	wgpu::TextureUsage usageFlags
)
{
	// Check if surface texture is valid
	if (!m_surfaceTexture)
	{
		spdlog::error("Cannot update render texture: surface texture not acquired");
		return nullptr;
	}

	auto viewPortWidth = static_cast<uint32_t>(m_surfaceTexture->getWidth() * viewport.z);
	auto viewPortHeight = static_cast<uint32_t>(m_surfaceTexture->getHeight() * viewport.w);

	if (viewPortWidth == 0 || viewPortHeight == 0)
	{
		spdlog::warn("Invalid viewport dimensions: {}x{}", viewPortWidth, viewPortHeight);
		return nullptr;
	}

	if (cpuTarget.has_value() && cpuTarget->valid())
	{
		auto textureOpt = cpuTarget->get();
		if (textureOpt.has_value())
		{
			uint32_t cpuWidth = textureOpt.value()->getWidth();
			uint32_t cpuHeight = textureOpt.value()->getHeight();

			if (!gpuTexture || !gpuTexture->matches(cpuWidth, cpuHeight, format))
			{
				gpuTexture = m_context->textureFactory().createFromHandle(cpuTarget.value());
			}
			return gpuTexture;
		}
	}

	if (!gpuTexture || !gpuTexture->matches(viewPortWidth, viewPortHeight, format))
	{
		gpuTexture = m_context->textureFactory().createRenderTarget(
			renderTargetId,
			viewPortWidth,
			viewPortHeight,
			format
		);
	}

	return gpuTexture;
}
void Renderer::renderToTexture(
	const RenderCollector &collector,
	const DebugRenderCollector &debugCollector,
	RenderTarget &renderTarget
)
{
	spdlog::debug("renderToTexture called: cameraId={}, renderItems={}, lights={}", renderTarget.cameraId, collector.getRenderItems().size(), collector.getLights().size());

	renderTarget.gpuTexture = updateRenderTexture(
		renderTarget.cameraId,
		renderTarget.gpuTexture,
		renderTarget.cpuTarget,
		renderTarget.viewport,
		wgpu::TextureFormat::RGBA16Float,
		static_cast<WGPUTextureUsage>(
			wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc
		)
	);

	if (!renderTarget.gpuTexture)
	{
		spdlog::error("Failed to create/update render target texture");
		return;
	}

	// Create render pass context
	auto renderPassContext = m_context->renderPassFactory().create(
		renderTarget.gpuTexture,
		m_depthBuffer,
		renderTarget.clearFlags,
		renderTarget.backgroundColor
	);

	auto debugRenderPassContext = m_context->renderPassFactory().create(
		renderTarget.gpuTexture,
		nullptr,
		ClearFlags::None,
		renderTarget.backgroundColor
	);

	auto cameraFrustum = engine::math::Frustum::fromViewProjection(renderTarget.viewProjectionMatrix);

	std::vector<size_t> visibleIndices = collector.extractVisible(cameraFrustum);

	// Prepare GPU resources once for this frame
	spdlog::debug("Preparing GPU resources for {} render items", visibleIndices.size());
	m_frameCache.prepareGPUResources(m_context, collector, visibleIndices);

	// Set dependencies for MeshPass
	m_meshPass->setRenderPassContext(renderPassContext);
	m_meshPass->setCameraId(renderTarget.cameraId); // Use renderTargetId (which is the camera ID)
	m_meshPass->setVisibleIndices(visibleIndices);
	m_meshPass->setShadowBindGroup(m_shadowPass->getShadowBindGroup());

	// Render using MeshPass with FrameCache
	spdlog::debug("Rendering {} GPU items using MeshPass", m_frameCache.gpuRenderItems.size());
	m_meshPass->render(m_frameCache);

	m_debugPass->setRenderPassContext(debugRenderPassContext);
	m_debugPass->setCameraId(renderTarget.cameraId);
	m_debugPass->setDebugCollector(&debugCollector);
	m_debugPass->render(m_frameCache);

	// Check if CPU readback was requested
	if (renderTarget.cpuTarget.has_value() && renderTarget.cpuTarget->valid())
	{
		auto textureOpt = renderTarget.cpuTarget->get();
		if (textureOpt.has_value() && textureOpt.value()->isReadbackRequested())
		{
			// Initiate async GPU-to-CPU readback
			auto readbackFuture = renderTarget.gpuTexture->readbackToCPUAsync(*m_context, textureOpt.value());
			textureOpt.value()->setReadbackFuture(std::move(readbackFuture));
		}
	}
}

void Renderer::compositeTexturesToSurface(
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	auto renderPassContext = m_context->renderPassFactory().create(m_surfaceTexture);
	m_compositePass->setRenderPassContext(renderPassContext);
	m_compositePass->render(m_frameCache);

	// If UI callback is provided, render UI in a separate pass
	if (uiCallback)
	{
		wgpu::CommandEncoderDescriptor encoderDesc{};
		encoderDesc.label = "UI Command Encoder";
		wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

		auto uiPassContext = m_context->renderPassFactory().create(
			m_surfaceTexture,
			nullptr,
			ClearFlags::None
		);
		wgpu::RenderPassEncoder renderPass = uiPassContext->begin(encoder);
		uiCallback(renderPass);
		uiPassContext->end(renderPass);

		m_context->submitCommandEncoder(encoder, "UI Commands");
	}
}

void Renderer::onResize(uint32_t width, uint32_t height)
{
	if (m_depthBuffer)
		m_depthBuffer->resize(*m_context, width, height);

	for (auto &[id, target] : m_renderTargets)
	{
		if (target.gpuTexture && !target.cpuTarget.has_value())
			target.gpuTexture.reset();
	}

	if (m_meshPass)
		m_meshPass->cleanup();

	if (m_compositePass)
		m_compositePass->cleanup();

	if (m_shadowPass)
		m_shadowPass->cleanup();

	spdlog::info("Renderer resized to {}x{}", width, height);
}

} // namespace engine::rendering
