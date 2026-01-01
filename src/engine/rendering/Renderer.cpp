#include "engine/rendering/Renderer.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/scene/nodes/CameraNode.h"

namespace engine::rendering
{

Renderer::Renderer(std::shared_ptr<webgpu::WebGPUContext> context) :
	m_context(context),
	m_pipelineManager(std::make_unique<webgpu::WebGPUPipelineManager>(*context)),
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
		wgpu::TextureFormat::Depth24Plus
	);

	if (!m_depthBuffer)
	{
		spdlog::error("Failed to create depth buffer");
		return false;
	}

	m_frameBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("frameUniforms");
	if (m_frameBindGroupLayout == nullptr)
	{
		spdlog::error("Failed to get global bind group layout for frameUniforms");
		return false;
	}
	m_lightBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("lightUniforms");
	if (m_lightBindGroupLayout == nullptr)
	{
		spdlog::error("Failed to get global bind group layout for lightUniforms");
		return false;
	}
	m_lightBindGroup = m_context->bindGroupFactory().createBindGroup(m_lightBindGroupLayout);

	spdlog::info("Renderer initialized successfully");
	return true;
}

void Renderer::updateLights(const std::vector<LightStruct> &lights)
{
	// Always write the header, even if there are no lights (count = 0)
	LightsBuffer header;
	header.count = static_cast<uint32_t>(lights.size());

	// Write header to the global lights buffer at offset 0
	m_lightBindGroup->updateBuffer(0, &header, sizeof(LightsBuffer), 0, m_context->getQueue());

	// Write light data if any lights exist at offset sizeof(LightsBuffer)
	if (!lights.empty())
	{
		m_lightBindGroup->updateBuffer(0, lights.data(), lights.size() * sizeof(LightStruct), sizeof(LightsBuffer), m_context->getQueue());
	}
}

void Renderer::bindFrameUniforms(wgpu::RenderPassEncoder renderPass, const FrameUniforms &frameUniforms)
{
	// ToDo: Cache frame bind group per camera
	auto frameBindGroup = m_context->bindGroupFactory().createBindGroup(m_frameBindGroupLayout);
	frameBindGroup->updateBuffer(0, &frameUniforms, sizeof(FrameUniforms), 0, m_context->getQueue());
	renderPass.setBindGroup(0, frameBindGroup->getBindGroup(), 0, nullptr);
}

void Renderer::bindLightUniforms(wgpu::RenderPassEncoder renderPass)
{
	renderPass.setBindGroup(1, m_lightBindGroup->getBindGroup(), 0, nullptr);
}

void Renderer::startFrame()
{
	m_surfaceTexture = m_context->surfaceManager().acquireNextTexture();
}

std::shared_ptr<webgpu::WebGPUTexture> Renderer::updateRenderTexture(
	std::shared_ptr<webgpu::WebGPUTexture> &gpuTexture,
	const std::optional<Texture::Handle> &cpuTarget,
	const glm::vec4 &viewport,
	wgpu::TextureFormat format,
	wgpu::TextureUsage usageFlags
)
{
	if (cpuTarget && cpuTarget->valid())
	{
		gpuTexture = m_context->textureFactory().createFromHandle(cpuTarget.value());
		return gpuTexture;
	}

	uint32_t viewPortWidth = static_cast<uint32_t>(m_surfaceTexture->getWidth() * viewport.z);
	uint32_t viewPortHeight = static_cast<uint32_t>(m_surfaceTexture->getHeight() * viewport.w);

	if (gpuTexture)
	{
		if (!gpuTexture->matches(viewPortWidth, viewPortHeight, format))
		{
			gpuTexture->resize(*m_context, viewPortWidth, viewPortHeight);
		}
	}
	else
	{
		wgpu::TextureDescriptor texDesc{};
		texDesc.size.width = viewPortWidth;
		texDesc.size.height = viewPortHeight;
		texDesc.size.depthOrArrayLayers = 1;
		texDesc.format = format;
		texDesc.usage = usageFlags;

		wgpu::TextureViewDescriptor viewDesc{};
		viewDesc.format = format;
		viewDesc.dimension = wgpu::TextureViewDimension::_2D;
		viewDesc.aspect = wgpu::TextureAspect::All;

		gpuTexture = m_context->textureFactory().createFromDescriptors(texDesc, viewDesc);
	}
	return gpuTexture;
}

void Renderer::renderToTexture(
	const RenderCollector &collector,
	uint64_t renderTargetId, // eindeutige ID für das RenderTarget
	const glm::vec4 &viewport,
	ClearFlags clearFlags,
	const glm::vec4 &backgroundColor,
	std::optional<TextureHandle> cpuTarget,
	const FrameUniforms &frameUniforms
)
{
	// Prüfen, ob ein RenderTarget für diese ID existiert
	RenderTarget &target = m_renderTargets[renderTargetId];
	uint32_t viewPortWidth = static_cast<uint32_t>(m_surfaceTexture->getWidth() * viewport.z);
	uint32_t viewPortHeight = static_cast<uint32_t>(m_surfaceTexture->getHeight() * viewport.w);

	// Update Properties
	target.viewport = viewport;
	target.clearFlags = clearFlags;
	target.backgroundColor = backgroundColor;
	target.cpuTarget = cpuTarget;
	target.gpuTexture =
		updateRenderTexture(
			target.gpuTexture,
			cpuTarget,
			viewport,
			wgpu::TextureFormat::RGBA8Unorm,
			wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc
		);
	// Rendering starten
	updateLights(collector.getLights());

	m_commandEncoder = m_context->getDevice().createCommandEncoder();

	m_renderer->renderFrame(
		collector,
		target.gpuTexture,
		frameUniforms,
		target.clearFlags,
		target.backgroundColor
	);

	m_context->getQueue().submit(1, &m_commandEncoder.finish());
}

void Renderer::renderCameraToTexture(
	const RenderCollector &collector,
	RenderTarget &target,
	const FrameUniforms &frameUniforms
)
{
	// ToDo: Light should be stored per camera or scene but not in the renderer
	updateLights(collector.getLights());
	auto viewPortWidth = static_cast<uint32_t>(m_surfaceTexture->getWidth() * target.viewport.z);
	auto viewPortHeight = static_cast<uint32_t>(m_surfaceTexture->getHeight() * target.viewport.w);

	if (target.cpuTarget)
	{
		if (target.gpuTexture == nullptr)
		{
			target.gpuTexture = m_context->textureFactory().createFromHandle(target.cpuTarget.value()->getHandle());
		}
		auto resizeResult = target.gpuTexture->resize(*m_context, target.cpuTarget.value()->getWidth(), target.cpuTarget.value()->getHeight());
		if (!resizeResult)
		{
			spdlog::error("Failed to resize GPU texture for CPU readback target");
			return;
		}
	}
	else
	{
		target.gpuTexture = m_context->textureFactory().create
	}

	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "Camera Command Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// 2. Create render pass
	auto renderPassDesc = m_context->renderPassFactory().createForTexture(
		target.gpuTexture,
		m_depthBuffer,
		target.clearFlags,
		target.backgroundColor
	);
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	bindFrameUniforms(renderPass, frameUniforms);
	bindLightUniforms(renderPass);

	// 3. Render all collected objects
	renderCollectorItems(renderPass, collector);

	renderPass.end();
	renderPass.release();

	// 4. Submit GPU commands
	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = "Camera Command Buffer";
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
	encoder.release();
	m_context->getQueue().submit(commands);
	commands.release();

	// 6. If offscreen (no cpuTarget), store for later compositing
	if (!target.cpuTarget)
	{
		m_offscreenTextures.push_back(target.gpuTexture);
	}
}

void Renderer::compositeTexturesToSurface(
	const std::vector<uint64_t> &targets,
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	auto swapchainTexture = m_context->surfaceManager().acquireNextTexture();

	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "Composite Command Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	auto mainPass = m_context->renderPassFactory().createDefault(swapchainTexture, m_depthBuffer);
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(mainPass);

	// Draw each camera texture to its portion of the screen
	for (auto &camera : cameras)
	{
		drawFullScreenQuad(renderPass, camera->getRenderTarget(), camera->getViewport());
	}

	// Draw UI on top (only once)
	if (uiCallback)
		uiCallback(renderPass);

	renderPass.end();
	renderPass.release();

	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = "Composite Command Buffer";
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
	encoder.release();
	m_context->getQueue().submit(commands);
	commands.release();

	m_context->getSurface().present();
	swapchainTexture.reset();
}

void Renderer::onResize(uint32_t width, uint32_t height)
{
	// Resize depth buffer
	if (m_depthBuffer)
	{
		m_depthBuffer->resize(*m_context, width, height);
	}

	// Update render pass attachments will happen on next frame
	spdlog::info("Renderer resized to {}x{}", width, height);
}

std::shared_ptr<webgpu::WebGPUModel> Renderer::getOrCreateWebGPUModel(
	const engine::core::Handle<engine::rendering::Model> &modelHandle
)
{
	if (!modelHandle.valid())
	{
		spdlog::error("Invalid model handle");
		return nullptr;
	}

	uint64_t modelId = modelHandle.id();

	// Check if we already have this model cached
	auto it = m_modelCache.find(modelId);
	if (it != m_modelCache.end())
	{
		return it->second;
	}

	// Create new WebGPUModel using the factory
	auto webgpuModel = m_context->modelFactory().createFromHandle(modelHandle);
	if (!webgpuModel)
	{
		spdlog::error("Failed to create WebGPUModel from handle");
		return nullptr;
	}

	// Cache it for future use
	m_modelCache[modelId] = webgpuModel;
	spdlog::debug("Created and cached WebGPUModel for model ID: {}", modelId);

	return webgpuModel;
}

void Renderer::renderDebugPrimitives(
	wgpu::RenderPassEncoder renderPass,
	const DebugRenderCollector &debugCollector
)
{
	// ToDo: Implement debug primitive rendering
	if (!m_debugShader || !m_debugShader->isValid())
	{
		spdlog::warn("Debug shader is null or invalid");
		return; // Debug shader not available
	}
	/*
		// Get the debug pipeline
		auto debugPipeline = m_pipelineManager->getOrCreatePipeline("debug");
		if (!debugPipeline)
		{
			spdlog::warn("Debug pipeline not found in pipeline manager");
			return; // Debug pipeline not available
		}

		if (!debugPipeline->isValid())
		{
			spdlog::warn("Debug pipeline is invalid");
			return; // Debug pipeline not available
		}

		uint32_t primitiveCount = static_cast<uint32_t>(debugCollector.getPrimitives().size());
		if (primitiveCount == 0)
		{
			return;
		}

		spdlog::debug("Rendering {} debug primitives", primitiveCount);
		renderPass.setPipeline(debugPipeline->getPipeline());

		m_debugBindGroup->updateBuffer(
			0, // binding 0
			debugCollector.getPrimitives().data(),
			debugCollector.getPrimitives().size() * sizeof(DebugPrimitive),
			0,
			m_context->getQueue()
		);
		renderPass.setBindGroup(1, m_debugBindGroup->getBindGroup(), 0, nullptr);

		constexpr uint32_t maxVertexCount = 32;
		renderPass.draw(maxVertexCount, primitiveCount, 0, 0);
		*/
}

} // namespace engine::rendering
