#include "engine/rendering/Renderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <spdlog/spdlog.h>
#include <limits>

#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/scene/nodes/CameraNode.h"

namespace engine::rendering
{

Renderer::Renderer(std::shared_ptr<webgpu::WebGPUContext> context) :
	m_context(context),
	m_renderPassManager(std::make_unique<RenderPassManager>(*context))
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

	m_compositePass = std::make_unique<CompositePass>(m_context);
	if (!m_compositePass->initialize())
	{
		spdlog::error("Failed to initialize CompositePass");
		return false;
	}
	if (!initializeShadowResources())
	{
		spdlog::error("Failed to initialize shadow resources");
		return false;
	}

	spdlog::info("Renderer initialized successfully");
	return true;
}

bool Renderer::renderFrame(
	std::vector<RenderTarget> &renderTargets,
	const RenderCollector &renderCollector,
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

	m_frameCache = FrameCache{};
	m_frameCache.lights = renderCollector.getLights();
	m_frameCache.renderTargets = std::move(renderTargets);
	m_frameCache.time = time;

	renderShadowMaps(renderCollector);

	std::vector<uint64_t> cameraIds;

	for (const auto &target : m_frameCache.renderTargets)
	{
		renderCameraInternal(renderCollector, target, m_frameCache.time);
		cameraIds.push_back(target.cameraId);
	}

	compositeTexturesToSurface(cameraIds, uiCallback);

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
		spdlog::warn("Surface texture has invalid dimensions: {}x{}", 
			m_surfaceTexture->getWidth(), m_surfaceTexture->getHeight());
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

	uint32_t viewPortWidth = static_cast<uint32_t>(m_surfaceTexture->getWidth() * viewport.z);
	uint32_t viewPortHeight = static_cast<uint32_t>(m_surfaceTexture->getHeight() * viewport.w);

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

void Renderer::renderShadowMaps(const RenderCollector &collector)
{
	// Set dependencies for ShadowPass
	m_shadowPass->setRenderCollector(&collector);
	m_shadowPass->setShadowResources(&m_shadowResources);
	m_shadowPass->setContext(m_context);

	// Delegate shadow rendering to ShadowPass
	m_shadowPass->render(m_frameCache);

	// Pass shadow bind group to MeshPass
	m_meshPass->setShadowBindGroup(m_shadowResources.bindGroup);
}


void Renderer::renderToTexture(
	const RenderCollector &collector,
	uint64_t renderTargetId,
	const glm::vec4 &viewport,
	ClearFlags clearFlags,
	const glm::vec4 &backgroundColor,
	std::optional<TextureHandle> cpuTarget,
	const FrameUniforms &frameUniforms
)
{
	spdlog::debug("renderToTexture called: targetId={}, renderItems={}, lights={}", renderTargetId, collector.getRenderItems().size(), collector.getLights().size());

	RenderTarget &target = m_renderTargets[renderTargetId];
	target.viewport = viewport;
	target.clearFlags = clearFlags;
	target.backgroundColor = backgroundColor;
	target.cpuTarget = cpuTarget;

	target.gpuTexture = updateRenderTexture(
		target.cameraId,
		target.gpuTexture,
		cpuTarget,
		viewport,
		wgpu::TextureFormat::RGBA16Float,
		static_cast<WGPUTextureUsage>(
			wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc
		)
	);

	if (!target.gpuTexture)
	{
		spdlog::error("Failed to create/update render target texture");
		return;
	}

	// Create render pass context
	auto renderPassContext = m_context->renderPassFactory().create(
		target.gpuTexture,
		m_depthBuffer,
		clearFlags,
		glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) // ToDo: use backgroundColor just debigging
	);

	auto cameraFrustum = engine::math::Frustum::fromViewProjection(frameUniforms.viewProjectionMatrix);

	std::vector<size_t> visibleIndices = collector.extractVisible(cameraFrustum);

	// Prepare GPU resources once for this frame
	spdlog::debug("Preparing GPU resources for {} render items", visibleIndices.size());
	m_frameCache.prepareGPUResources(m_context, collector, visibleIndices);

	// Set dependencies for MeshPass
	m_meshPass->setRenderPassContext(renderPassContext);
	m_meshPass->setFrameUniforms(frameUniforms);
	m_meshPass->setCameraId(renderTargetId);
	m_meshPass->setVisibleIndices(visibleIndices);

	// Render using MeshPass with FrameCache
	spdlog::debug("Rendering {} GPU items using MeshPass", m_frameCache.gpuRenderItems.size());
	m_meshPass->render(m_frameCache);

	// Check if CPU readback was requested
	if (cpuTarget.has_value() && cpuTarget->valid())
	{
		auto textureOpt = cpuTarget->get();
		if (textureOpt.has_value() && textureOpt.value()->isReadbackRequested())
		{
			// Initiate async GPU-to-CPU readback
			auto readbackFuture = target.gpuTexture->readbackToCPUAsync(*m_context, textureOpt.value());
			textureOpt.value()->setReadbackFuture(std::move(readbackFuture));
		}
	}
}

void Renderer::renderCameraInternal(
	const RenderCollector &renderCollector,
	const RenderTarget &target,
	float frameTime
)
{
	// Render to texture
	renderToTexture(
		renderCollector,
		target.cameraId,
		target.viewport,
		target.clearFlags,
		target.backgroundColor,
		target.cpuTarget,
		target.getFrameUniforms(frameTime)
	);
}

void Renderer::compositeTexturesToSurface(
	const std::vector<uint64_t> &targetIds,
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	spdlog::debug("compositeTexturesToSurface called with {} target IDs", targetIds.size());

	// Filter FrameCache render targets by the provided IDs
	// Create a temporary FrameCache with only the targets we want
	FrameCache filteredCache;
	filteredCache.renderTargets.reserve(targetIds.size());

	for (uint64_t targetId : targetIds)
	{
		// Find the matching render target in the frame cache
		for (const auto& target : m_frameCache.renderTargets)
		{
			if (target.cameraId == targetId && target.gpuTexture)
			{
				filteredCache.renderTargets.push_back(target);
				break;
			}
		}
	}

	// Composite all textures in one render pass
	if (!filteredCache.renderTargets.empty())
	{
		auto renderPassContext = m_context->renderPassFactory().create(m_surfaceTexture);
		m_compositePass->setRenderPassContext(renderPassContext);
		m_compositePass->render(filteredCache);
	}

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
		wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(uiPassContext->getRenderPassDescriptor());

		uiCallback(renderPass);

		renderPass.end();
		renderPass.release();

		wgpu::CommandBufferDescriptor cmdBufferDesc{};
		cmdBufferDesc.label = "UI Command Buffer";
		wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
		encoder.release();
		m_context->getQueue().submit(commands);
		commands.release();
	}

	m_context->getSurface().present();
	m_surfaceTexture.reset();

	// Process any pending pipeline reloads after frame is complete
	m_context->pipelineManager().processPendingReloads();
}

void Renderer::onResize(uint32_t width, uint32_t height)
{
	if (m_depthBuffer)
	{
		m_depthBuffer->resize(*m_context, width, height);
	}

	for (auto &[id, target] : m_renderTargets)
	{
		if (target.gpuTexture && !target.cpuTarget.has_value())
		{
			target.gpuTexture.reset();
		}
	}

	// Clear pass caches
	// ToDo: Consider if we need more granular cache invalidation
	if (m_meshPass)
	{
		m_meshPass->clearFrameBindGroupCache();
	}

	if (m_compositePass)
	{
		m_compositePass->cleanup();
	}

	if (m_shadowPass)
	{
		m_shadowPass->cleanup();
	}

	spdlog::info("Renderer resized to {}x{}", width, height);
}

void Renderer::renderDebugPrimitives(
	wgpu::RenderPassEncoder renderPass,
	const DebugRenderCollector &debugCollector
)
{
	// ToDo: Implement debug primitive rendering (shader, pipeline, bind groups)
}

bool Renderer::initializeShadowResources()
{
	if (m_shadowResources.initialized)
		return true;
	auto shadowLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("shadowMaps");
	if (shadowLayout == nullptr)
	{
		spdlog::error("Failed to get shadowMaps bind group layout");
		return false;
	}

	m_shadowResources.shadowSampler = m_context->samplerFactory().getShadowComparisonSampler();

	// Create 2D shadow map array
	m_shadowResources.shadow2DArray = m_context->textureFactory().createShadowMap2DArray(
		constants::DEFAULT_SHADOW_MAP_SIZE,
		constants::MAX_SHADOW_MAPS_2D
	);

	// Create cube shadow map array
	m_shadowResources.shadowCubeArray = m_context->textureFactory().createShadowMapCubeArray(
		constants::DEFAULT_CUBE_SHADOW_MAP_SIZE,
		constants::MAX_SHADOW_MAPS_CUBE
	);

	std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> resources = {
		{{4, 0}, webgpu::BindGroupResource(m_shadowResources.shadowSampler)},
		{{4, 1}, webgpu::BindGroupResource(m_shadowResources.shadow2DArray)},
		{{4, 2}, webgpu::BindGroupResource(m_shadowResources.shadowCubeArray)}
	};

	// Create bind group (sampler + textures + storage buffers)
	m_shadowResources.bindGroup = m_context->bindGroupFactory().createBindGroup(
		shadowLayout,
		resources,
		nullptr,
		"ShadowMaps"
	);

	m_shadowResources.initialized = true;
	return true;
}

} // namespace engine::rendering
