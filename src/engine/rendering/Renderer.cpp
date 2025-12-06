#include "engine/rendering/Renderer.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/BindGroupLayout.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/resources/ResourceManager.h"

namespace engine::rendering
{

Renderer::Renderer(std::shared_ptr<webgpu::WebGPUContext> context)
	: m_context(context)
	, m_pipelineManager(std::make_unique<PipelineManager>(*context))
	, m_renderPassManager(std::make_unique<RenderPassManager>(*context))
{
}

Renderer::~Renderer()
{
	if (m_frameUniformBuffer)
	{
		m_frameUniformBuffer.destroy();
		m_frameUniformBuffer.release();
	}

	if (m_lightsBuffer)
	{
		m_lightsBuffer.destroy();
		m_lightsBuffer.release();
	}
	
	if (m_objectUniformBuffer)
	{
		m_objectUniformBuffer.destroy();
		m_objectUniformBuffer.release();
	}
}

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

	// Create frame uniform buffer
	wgpu::BufferDescriptor frameUniformDesc{};
	frameUniformDesc.label = "Frame Uniforms";
	frameUniformDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
	frameUniformDesc.size = sizeof(FrameUniforms);
	frameUniformDesc.mappedAtCreation = false;
	m_frameUniformBuffer = m_context->getDevice().createBuffer(frameUniformDesc);

	if (!m_frameUniformBuffer)
	{
		spdlog::error("Failed to create frame uniform buffer");
		return false;
	}

	// Create lights buffer
	const size_t maxLights = 16;
	const size_t lightsBufferSize = sizeof(LightsBuffer) + maxLights * sizeof(LightStruct);
	m_lightsBuffer = m_context->bufferFactory().createStorageBuffer(lightsBufferSize);

	if (!m_lightsBuffer)
	{
		spdlog::error("Failed to create lights buffer");
		return false;
	}

	// Create object uniform buffer
	m_objectUniformBuffer = m_context->bufferFactory().createUniformBuffer(sizeof(ObjectUniforms));

	if (!m_objectUniformBuffer)
	{
		spdlog::error("Failed to create object uniform buffer");
		return false;
	}

	// Setup default pipelines (must be done before creating bind groups)
	if (!setupDefaultPipelines())
	{
		spdlog::error("Failed to setup pipelines");
		return false;
	}

	// Create frame bind group (after pipeline setup so layout exists)
	if (!createFrameBindGroup())
	{
		spdlog::error("Failed to create frame bind group");
		return false;
	}

	// Create light bind group (after pipeline setup so layout exists)
	if (!createLightBindGroup())
	{
		spdlog::error("Failed to create light bind group");
		return false;
	}

	// Setup default render passes
	if (!setupDefaultRenderPasses())
	{
		spdlog::error("Failed to setup render passes");
		return false;
	}

	spdlog::info("Renderer initialized successfully");
	return true;
}

void Renderer::updateFrameUniforms(const FrameUniforms &frameUniforms)
{
	m_context->getQueue().writeBuffer(
		m_frameUniformBuffer,
		0,
		&frameUniforms,
		sizeof(FrameUniforms)
	);
}

void Renderer::renderFrame(
	const RenderCollector &collector,
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	// 1. Update lights from collected data
	updateLights(collector.getLights());

	// 2. Acquire next surface texture
	auto surfaceTexture = m_context->surfaceManager().acquireNextTexture();

	// 3. Create command encoder
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "Frame Command Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// 43. Create or update render pass
	if (m_mainPassId == 0)
	{
		// First frame - create the render pass with the acquired surface texture
		auto mainPass = m_context->renderPassFactory().createDefault(
			surfaceTexture,
			m_depthBuffer
		);

		m_renderPassManager->registerPass(mainPass);
		m_mainPassId = mainPass->getId();
	}
	else
	{
		// Subsequent frames - update the render pass attachments
		m_renderPassManager->updatePassAttachments(
			m_mainPassId,
			surfaceTexture,
			m_depthBuffer
		);
	}

	// 5. Begin main render pass
	wgpu::RenderPassEncoder renderPass = m_renderPassManager->beginPass(m_mainPassId, encoder);

	// Keep bind groups alive until after render pass ends
	std::vector<wgpu::BindGroup> objectBindGroups;

	// 6. Get main pipeline
	auto mainPipeline = m_pipelineManager->getPipeline("main");
	if (mainPipeline && mainPipeline->isValid())
	{
		renderPass.setPipeline(mainPipeline->getPipeline());

		// 7. Bind frame-level bind group (group 0: camera, time, etc.) - owned by Renderer
		renderPass.setBindGroup(0, m_frameBindGroup->getBindGroup(), 0, nullptr);

		// 8. Bind light bind group (group 1: lighting data)
		if (m_lightBindGroup)
		{
			renderPass.setBindGroup(1, m_lightBindGroup->getBindGroup(), 0, nullptr);
		}

		// 9. Render all collected items
		objectBindGroups.reserve(collector.getRenderItems().size());

		for (const auto &item : collector.getRenderItems())
		{
			// Get or create WebGPUModel for this handle
			auto webgpuModel = getOrCreateWebGPUModel(item.model);
			if (!webgpuModel)
			{
				spdlog::warn("Failed to get WebGPUModel for render item");
				continue;
			}

			// Update object uniforms with world transform
			ObjectUniforms objectUniforms;
			objectUniforms.modelMatrix = item.worldTransform;
			objectUniforms.normalMatrix = glm::transpose(glm::inverse(item.worldTransform));

			m_context->getQueue().writeBuffer(
				m_objectUniformBuffer,
				0,
				&objectUniforms,
				sizeof(ObjectUniforms)
			);

			// Create bind group for object uniforms (group 2)
			wgpu::BindGroupEntry objectEntry = {};
			objectEntry.binding = 0;
			objectEntry.buffer = m_objectUniformBuffer;
			objectEntry.offset = 0;
			objectEntry.size = sizeof(ObjectUniforms);

			auto objectBindGroup = m_context->bindGroupFactory().createBindGroup(
				m_objectUniformLayout->getLayout(),
				{objectEntry}
			);

			renderPass.setBindGroup(2, objectBindGroup, 0, nullptr);

			// Update and render the model (group 3: material will be set inside)
			webgpuModel->update();
			webgpuModel->render(encoder, renderPass);

			// Keep bind group alive - don't release until after render pass ends
			objectBindGroups.push_back(objectBindGroup);
		}
	}

	// 10. Render UI/debug overlays if callback provided
	if (uiCallback)
	{
		uiCallback(renderPass);
	}

	// 11. End render pass
	renderPass.end();
	renderPass.release();

	// 12. Finish encoder and submit commands
	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = "Frame Command Buffer";
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);

	// 13. Release resources after encoding is complete
	encoder.release();

	// Release object bind groups
	for (auto &bindGroup : objectBindGroups)
	{
		bindGroup.release();
	}

	m_context->getQueue().submit(commands);
	commands.release();

	// 12. Present frame
	m_context->getSurface().present();

	// 13. Release surface texture to allow next frame acquisition
	// This is critical - the surface texture must be dropped before the next getCurrentTexture()
	surfaceTexture.reset();
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

bool Renderer::setupDefaultPipelines()
{
	// Load main shader module
	auto mainShaderModule = engine::resources::ResourceManager::loadShaderModule(
		engine::core::PathProvider::getResource("shader.wgsl"),
		m_context->getDevice()
	);

	if (!mainShaderModule)
	{
		spdlog::error("Failed to load main shader module");
		return false;
	}

	// Create shader info with combined vertex/fragment entry points
	auto mainShaderInfo = std::make_shared<webgpu::WebGPUShaderInfo>(
		mainShaderModule,
		"vs_main",  // vertex entry point
		"fs_main"   // fragment entry point
	);

	// Create bind group layouts
	auto frameLayout = m_context->bindGroupFactory().createCustomBindGroupLayout(
		m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<FrameUniforms>()
	);

	m_lightBindGroupLayout = m_context->bindGroupFactory().createDefaultLightingBindGroupLayout();

	m_objectUniformLayout = m_context->bindGroupFactory().createCustomBindGroupLayout(
		m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<ObjectUniforms>()
	);

	auto materialLayout = m_context->bindGroupFactory().createDefaultMaterialBindGroupLayout();

	// Main rendering pipeline
	PipelineConfig mainConfig;
	mainConfig.shaderInfo = mainShaderInfo;
	mainConfig.colorFormat = m_context->getSwapChainFormat();
	mainConfig.depthFormat = wgpu::TextureFormat::Depth24Plus;
	mainConfig.bindGroupLayouts = {frameLayout, m_lightBindGroupLayout, m_objectUniformLayout, materialLayout};
	mainConfig.enableDepth = true;
	mainConfig.topology = wgpu::PrimitiveTopology::TriangleList;
	mainConfig.vertexBufferCount = 1;

	if (!m_pipelineManager->createPipeline("main", mainConfig))
	{
		spdlog::error("Failed to create main pipeline");
		return false;
	}

	// Load debug shader module
	auto debugShaderModule = engine::resources::ResourceManager::loadShaderModule(
		engine::core::PathProvider::getResource("debug.wgsl"),
		m_context->getDevice()
	);

	if (!debugShaderModule)
	{
		spdlog::warn("Failed to load debug shader module - debug rendering will be unavailable");
		return true; // Don't fail initialization if debug shader fails
	}

	// Create debug shader info
	auto debugShaderInfo = std::make_shared<webgpu::WebGPUShaderInfo>(
		debugShaderModule,
		"vs_main",
		"fs_main"
	);

	// Debug pipeline for line rendering
	PipelineConfig debugConfig;
	debugConfig.shaderInfo = debugShaderInfo;
	debugConfig.colorFormat = m_context->getSwapChainFormat();
	debugConfig.depthFormat = wgpu::TextureFormat::Depth24Plus;
	debugConfig.topology = wgpu::PrimitiveTopology::LineList;
	debugConfig.vertexBufferCount = 0;
	debugConfig.enableDepth = true;

	auto debugLayout = m_context->bindGroupFactory().createCustomBindGroupLayout(
		m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<glm::mat4x4>(
			-1,
			static_cast<uint32_t>(wgpu::ShaderStage::Vertex)
		),
		m_context->bindGroupFactory().createStorageBindGroupLayoutEntry(
			-1,
			wgpu::ShaderStage::Vertex,
			true
		)
	);

	debugConfig.bindGroupLayouts = {debugLayout};

	if (!m_pipelineManager->createPipeline("debug", debugConfig))
	{
		spdlog::warn("Failed to create debug pipeline");
	}

	return true;
}

bool Renderer::setupDefaultRenderPasses()
{
	// Don't create the render pass here - we'll create it on the first frame
	// when we have a valid surface texture to work with.
	// This avoids acquiring a surface texture during initialization that never gets released.
	return true;
}

bool Renderer::createFrameBindGroup()
{
	// Create frame bind group layout
	auto frameLayout = m_context->bindGroupFactory().createCustomBindGroupLayout(
		m_context->bindGroupFactory().createUniformBindGroupLayoutEntry<FrameUniforms>()
	);

	if (!frameLayout)
	{
		spdlog::error("Failed to create frame bind group layout");
		return false;
	}

	// Create bind group with frame uniform buffer
	wgpu::BindGroupEntry frameEntry = {};
	frameEntry.binding = 0;
	frameEntry.buffer = m_frameUniformBuffer;
	frameEntry.offset = 0;
	frameEntry.size = sizeof(FrameUniforms);

	auto rawBindGroup = m_context->bindGroupFactory().createBindGroup(
		frameLayout->getLayout(),
		{frameEntry}
	);

	if (!rawBindGroup)
	{
		spdlog::error("Failed to create frame bind group");
		return false;
	}

	m_frameBindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
		rawBindGroup,
		frameLayout,
		std::vector<wgpu::Buffer>{}  // Empty - Renderer owns the buffer
	);

	spdlog::info("Frame bind group created successfully");
	return true;
}

bool Renderer::createLightBindGroup()
{
	if (!m_lightBindGroupLayout)
	{
		spdlog::error("Light bind group layout not created yet!");
		return false;
	}

	// Create bind group with lights buffer
	wgpu::BindGroupEntry lightEntry = {};
	lightEntry.binding = 0;
	lightEntry.buffer = m_lightsBuffer;
	lightEntry.offset = 0;
	lightEntry.size = sizeof(LightsBuffer) + 16 * sizeof(LightStruct); // Max 16 lights

	auto rawBindGroup = m_context->bindGroupFactory().createBindGroup(
		m_lightBindGroupLayout->getLayout(),
		{lightEntry}
	);

	if (!rawBindGroup)
	{
		spdlog::error("Failed to create light bind group");
		return false;
	}

	m_lightBindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
		rawBindGroup,
		m_lightBindGroupLayout,
		std::vector<wgpu::Buffer>{m_lightsBuffer}
	);

	spdlog::info("Light bind group created successfully");
	return true;
}

void Renderer::updateLights(const std::vector<LightStruct> &lights)
{
	// Always write the header, even if there are no lights (count = 0)
	LightsBuffer header;
	header.count = static_cast<uint32_t>(lights.size());

	m_context->getQueue().writeBuffer(m_lightsBuffer, 0, &header, sizeof(LightsBuffer));

	// Write light data if any lights exist
	if (!lights.empty())
	{
		m_context->getQueue().writeBuffer(
			m_lightsBuffer,
			sizeof(LightsBuffer),
			lights.data(),
			lights.size() * sizeof(LightStruct)
		);
	}
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

} // namespace engine::rendering
