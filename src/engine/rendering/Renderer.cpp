#include "engine/rendering/Renderer.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/BindGroupLayout.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUModelFactory.h"
#include "engine/resources/ResourceManager.h"

namespace engine::rendering
{

Renderer::Renderer(std::shared_ptr<webgpu::WebGPUContext> context) : m_context(context), m_pipelineManager(std::make_unique<PipelineManager>(*context)), m_renderPassManager(std::make_unique<RenderPassManager>(*context))
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

	// Setup default pipelines
	if (!setupDefaultPipelines())
	{
		spdlog::error("Failed to setup pipelines");
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
	// Update the global frame uniforms buffer shared by all shaders
	// Both main and debug shaders reference this via ShaderFactory's global buffer cache
	m_context->updateGlobalBuffer("frameUniforms", frameUniforms);
}

void Renderer::renderFrame(
	const RenderCollector &collector,
	const DebugRenderCollector *debugCollector,
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

	// 7. Render all collected items
	std::string currentPipelineName = ""; // Track current pipeline to avoid redundant switches
	std::shared_ptr<webgpu::WebGPUShaderInfo> currentShader = nullptr;

	for (const auto &item : collector.getRenderItems())
	{
		// Get or create WebGPUModel for this handle
		auto webgpuModel = getOrCreateWebGPUModel(item.model);
		if (!webgpuModel)
		{
			spdlog::warn("Failed to get WebGPUModel for render item");
			continue;
		}

		// Determine which pipeline to use based on material
		std::string pipelineName = "main"; // Default
		auto material = webgpuModel->getMaterial();
		if (material)
		{
			pipelineName = material->getPipelineName();
		}

		// Set pipeline if it changed (avoid redundant pipeline switches)
		if (pipelineName != currentPipelineName)
		{
			// ToDo: beginPass for different pipelines?
			auto pipeline = m_pipelineManager->getPipeline(pipelineName);
			if (pipeline && pipeline->isValid())
			{
				renderPass.setPipeline(pipeline->getPipeline());
				currentPipelineName = pipelineName;

				// Get the shader info for this pipeline
				currentShader = m_pipelineManager->getShaderInfo(pipelineName);

				// Set the pipeline handle on the material immediately when pipeline changes
				if (material)
				{
					material->setPipelineHandle(pipeline->getHandle());
				}
			}
			else
			{
				spdlog::warn("Pipeline '{}' not found, skipping render item", pipelineName);
				continue;
			}
		}

		if (!currentShader)
		{
			spdlog::warn("No shader info for pipeline '{}'", pipelineName);
			continue;
		}

		// Update object uniforms (Group 2)
		// Object uniforms (model/normal matrices) change per object, so we must write them
		// for each draw call to the shared global buffer
		ObjectUniforms objectUniforms;
		objectUniforms.modelMatrix = item.worldTransform;
		objectUniforms.normalMatrix = glm::transpose(glm::inverse(item.worldTransform));

		// Set queue and render pass for current shader
		currentShader->setQueue(m_context->getQueue());

		// Write to shader's bind group 2, binding 0 (object uniforms)
		// Use template version for implicit size
		currentShader->updateBindGroupBuffer(2, 0, objectUniforms);

		// Update and render the model
		// update() triggers dirty-flag-based updates for mesh data
		webgpuModel->update();
		currentShader->bind(renderPass);
		webgpuModel->render(encoder, renderPass);
	}

	// 8. Render debug primitives if provided
	if (debugCollector && !debugCollector->getPrimitives().empty())
	{
		renderDebugPrimitives(renderPass, *debugCollector);
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
	// Get the lit shader from ShaderRegistry (already initialized in WebGPUContext)
	m_mainShader = m_context->shaderRegistry().getShader(ShaderType::Lit);

	if (!m_mainShader || !m_mainShader->isValid())
	{
		spdlog::error("Failed to get Lit shader from ShaderRegistry");
		return false;
	}

	// Extract the created bind group layout infos from the shader
	const auto &layoutInfos = m_mainShader->getBindGroupLayoutInfos();
	if (layoutInfos.size() < 4)
	{
		spdlog::error("ShaderFactory didn't create expected bind group layouts (got {}, expected 4)", layoutInfos.size());
		return false;
	}

	// Main rendering pipeline
	PipelineConfig mainConfig;
	mainConfig.shaderInfo = m_mainShader;
	mainConfig.colorFormat = m_context->getSwapChainFormat();
	mainConfig.depthFormat = wgpu::TextureFormat::Depth24Plus;
	mainConfig.bindGroupLayouts = layoutInfos;
	mainConfig.enableDepth = true;
	mainConfig.topology = wgpu::PrimitiveTopology::TriangleList;
	mainConfig.vertexBufferCount = 1;

	if (!m_pipelineManager->createPipeline("main", mainConfig))
	{
		spdlog::error("Failed to create main pipeline");
		return false;
	}

	// Get the debug shader from ShaderRegistry
	m_debugShader = m_context->shaderRegistry().getShader(ShaderType::Debug);

	if (!m_debugShader || !m_debugShader->isValid())
	{
		spdlog::warn("Failed to get Debug shader from ShaderRegistry - debug rendering will be unavailable");
		return true; // Don't fail initialization if debug shader fails
	}

	// Debug pipeline for line rendering
	PipelineConfig debugConfig;
	debugConfig.shaderInfo = m_debugShader;
	debugConfig.colorFormat = m_context->getSwapChainFormat();
	debugConfig.depthFormat = wgpu::TextureFormat::Depth24Plus;
	debugConfig.topology = wgpu::PrimitiveTopology::LineList;
	debugConfig.vertexBufferCount = 0;
	debugConfig.enableDepth = true;

	// Use ShaderFactory-generated layout
	auto debugLayoutInfos = m_debugShader->getBindGroupLayoutInfos();
	if (debugLayoutInfos.empty())
	{
		spdlog::error("Debug shader has no bind group layouts");
		return false;
	}

	spdlog::debug("Debug shader has {} bind group layouts", debugLayoutInfos.size());
	debugConfig.bindGroupLayouts = debugLayoutInfos;

	if (!m_pipelineManager->createPipeline("debug", debugConfig))
	{
		spdlog::error("Failed to create debug pipeline - debug rendering will be unavailable");
		m_debugShader = nullptr; // Clear the shader so we don't try to use it
		return true;			 // Don't fail initialization
	}

	spdlog::info("Successfully created debug pipeline");
	spdlog::info("Successfully created pipelines from ShaderRegistry");
	return true;
}

bool Renderer::setupDefaultRenderPasses()
{
	// Don't create the render pass here - we'll create it on the first frame
	// when we have a valid surface texture to work with.
	// This avoids acquiring a surface texture during initialization that never gets released.
	return true;
}

void Renderer::updateLights(const std::vector<LightStruct> &lights)
{
	// Always write the header, even if there are no lights (count = 0)
	LightsBuffer header;
	header.count = static_cast<uint32_t>(lights.size());

	// Write header to the global lights buffer at offset 0
	m_context->updateGlobalBuffer("lightUniforms", &header, sizeof(LightsBuffer));

	// Write light data if any lights exist at offset sizeof(LightsBuffer)
	if (!lights.empty())
	{
		// Get the global buffer and write light array at the correct offset
		auto buffer = m_context->shaderFactory().getGlobalBuffer("lightUniforms");
		if (buffer)
		{
			m_context->getQueue().writeBuffer(
				buffer->getBuffer(),
				sizeof(LightsBuffer), // Offset after the header
				lights.data(),
				lights.size() * sizeof(LightStruct)
			);
		}
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

void Renderer::renderDebugPrimitives(
	wgpu::RenderPassEncoder renderPass,
	const DebugRenderCollector &debugCollector
)
{
	if (!m_debugShader || !m_debugShader->isValid())
	{
		spdlog::warn("Debug shader is null or invalid");
		return; // Debug shader not available
	}

	// Get the debug pipeline
	auto debugPipeline = m_pipelineManager->getPipeline("debug");
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

	// Set queue for buffer updates
	m_debugShader->setQueue(m_context->getQueue());

	// Update the debug primitive storage buffer (group 1, binding 0)
	// This writes the primitive data directly to the shader's existing storage buffer
	m_debugShader->updateBindGroupBuffer(
		1, // group 1 (separate from frame uniforms in group 0)
		0, // binding 0
		debugCollector.getPrimitives().data(),
		debugCollector.getPrimitives().size() * sizeof(DebugPrimitive),
		0
	);

	// Set the debug pipeline
	renderPass.setPipeline(debugPipeline->getPipeline());

	// Bind all shader bind groups (frame uniforms + primitive buffer) to the render pass
	m_debugShader->bind(renderPass);

	// Draw primitives with correct vertex counts per type
	for (uint32_t i = 0; i < primitiveCount; ++i)
	{
		const auto& primitive = debugCollector.getPrimitives()[i];
		uint32_t vertexCount = 2; // Default for lines

		switch (static_cast<DebugPrimitiveType>(primitive.type))
		{
		case DebugPrimitiveType::Line:
			vertexCount = 2;
			break;
		case DebugPrimitiveType::Disk:
			vertexCount = 32;
			break;
		case DebugPrimitiveType::AABB:
			vertexCount = 24; // 12 edges * 2 vertices
			break;
		case DebugPrimitiveType::Arrow:
			vertexCount = 10; // shaft (2) + head (8)
			break;
		}

		// Draw this primitive instance
		renderPass.draw(vertexCount, 1, 0, i);
	}
}

} // namespace engine::rendering
