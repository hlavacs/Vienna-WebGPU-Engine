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
	m_pipelineManager(std::make_unique<webgpu::WebGPUPipelineManager>(*context)),
	m_renderPassManager(std::make_unique<RenderPassManager>(*context))
{
}

Renderer::~Renderer() = default;

bool Renderer::initialize()
{
	spdlog::info("Initializing Renderer");

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

	m_objectBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("objectUniforms");
	if (m_objectBindGroupLayout == nullptr)
	{
		spdlog::error("Failed to get global bind group layout for objectUniforms");
		return false;
	}

	wgpu::SamplerDescriptor samplerDesc{};
	samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
	samplerDesc.magFilter = wgpu::FilterMode::Linear;
	samplerDesc.minFilter = wgpu::FilterMode::Linear;
	samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
	samplerDesc.maxAnisotropy = 1;
	m_fullscreenQuadSampler = m_context->getDevice().createSampler(samplerDesc);

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

void Renderer::bindFrameUniforms(wgpu::RenderPassEncoder renderPass, uint64_t cameraId, const FrameUniforms &frameUniforms)
{
	auto &frameBindGroup = m_frameBindGroupCache[cameraId];
	if (!frameBindGroup)
	{
		frameBindGroup = m_context->bindGroupFactory().createBindGroup(m_frameBindGroupLayout);
	}

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
			viewPortWidth,
			viewPortHeight,
			format
		);
	}

	return gpuTexture;
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
	spdlog::debug("renderToTexture called: targetId={}, renderItems={}, lights={}", 
		renderTargetId, collector.getRenderItems().size(), collector.getLights().size());

	updateLights(collector.getLights());

	RenderTarget &target = m_renderTargets[renderTargetId];

	target.viewport = viewport;
	target.clearFlags = clearFlags;
	target.backgroundColor = backgroundColor;
	target.cpuTarget = cpuTarget;

	target.gpuTexture = updateRenderTexture(
		target.gpuTexture,
		cpuTarget,
		viewport,
		wgpu::TextureFormat::RGBA8Unorm,
		static_cast<WGPUTextureUsage>(
			wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc
		)
	);

	if (!target.gpuTexture)
	{
		spdlog::error("Failed to create/update render target texture");
		return;
	}

	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = ("RenderTarget_" + std::to_string(renderTargetId) + "_Encoder").c_str();
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	auto renderPassContext = m_context->renderPassFactory().createForTexture(
		target.gpuTexture,
		m_depthBuffer,
		clearFlags,
		backgroundColor
	);
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

	bindFrameUniforms(renderPass, renderTargetId, frameUniforms);
	bindLightUniforms(renderPass);
	
	spdlog::debug("About to call renderItems with {} items", collector.getRenderItems().size());
	renderItems(encoder, renderPass, collector, renderPassContext);
	spdlog::debug("Returned from renderItems");

	renderPass.end();
	renderPass.release();

	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = ("RenderTarget_" + std::to_string(renderTargetId) + "_CommandBuffer").c_str();
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
	encoder.release();
	m_context->getQueue().submit(commands);
	commands.release();

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

void Renderer::compositeTexturesToSurface(
	const std::vector<uint64_t> &targetIds,
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	spdlog::debug("compositeTexturesToSurface called with {} target IDs", targetIds.size());
	for (auto id : targetIds)
	{
		spdlog::debug("  - Target ID: {}", id);
	}

	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "Composite Command Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// Create render pass WITHOUT depth buffer for compositing (no depth testing needed)
	auto mainPass = m_context->renderPassFactory().createDefault(m_surfaceTexture, nullptr);
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(mainPass->getRenderPassDescriptor());

	std::vector<std::pair<std::shared_ptr<webgpu::WebGPUTexture>, glm::vec4>> texturesWithViewports;
	for (uint64_t targetId : targetIds)
	{
		auto it = m_renderTargets.find(targetId);
		if (it != m_renderTargets.end() && it->second.gpuTexture)
		{
			texturesWithViewports.emplace_back(it->second.gpuTexture, it->second.viewport);
		}
	}

	if (!texturesWithViewports.empty())
	{
		drawFullscreenQuads(renderPass, texturesWithViewports);
	}

	if (uiCallback)
	{
		uiCallback(renderPass);
	}

	renderPass.end();
	renderPass.release();

	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = "Composite Command Buffer";
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
	encoder.release();
	m_context->getQueue().submit(commands);
	commands.release();

	m_context->getSurface().present();
	m_surfaceTexture.reset();
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

	m_frameBindGroupCache.clear();
	m_fullscreenQuadBindGroupCache.clear();
	m_fullscreenQuadPipeline.reset();

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

	// Check if we already have this model cached
	auto it = m_modelCache.find(modelHandle);
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
	m_modelCache[modelHandle] = webgpuModel;

	return webgpuModel;
}

void Renderer::renderItems(
	wgpu::CommandEncoder &encoder,
	wgpu::RenderPassEncoder renderPass,
	const RenderCollector &collector,
	const std::shared_ptr<webgpu::WebGPURenderPassContext> &renderPassContext
)
{
	// Set viewport and scissor for the render pass
	auto colorTexture = renderPassContext->getColorTexture(0);
	if (colorTexture)
	{
		uint32_t width = colorTexture->getWidth();
		uint32_t height = colorTexture->getHeight();
		
		renderPass.setViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
		renderPass.setScissorRect(0, 0, width, height);
		
		spdlog::debug("Set viewport and scissor: {}x{}", width, height);
	}

	// Track current state to avoid redundant binding
	uint64_t currentModelId = 0;
	uint64_t currentMaterialId = 0;
	std::shared_ptr<webgpu::WebGPUPipeline> currentPipeline = nullptr;

	spdlog::debug("renderItems: Processing {} render items", collector.getRenderItems().size());

	// Each RenderItem represents a specific submesh to render
	for (const auto &item : collector.getRenderItems())
	{
		spdlog::debug("Processing render item with modelHandle id={}", item.modelHandle.id());

		// Get the GPU model
		auto gpuModel = getOrCreateWebGPUModel(item.modelHandle);
		if (!gpuModel)
		{
			spdlog::warn("Failed to get/create GPU model, skipping");
			continue;
		}

		// Update GPU resources from CPU (lazy update based on version)
		gpuModel->update();

		auto mesh = gpuModel->getMesh();
		if (!mesh)
		{
			spdlog::warn("GPU model has no mesh, skipping");
			continue;
		}

		// Update mesh GPU resources if needed
		mesh->update();

		uint64_t modelId = item.modelHandle.id();

		// When model changes: update vertex/index buffers AND object uniforms
		if (modelId != currentModelId)
		{
			currentModelId = modelId;

			spdlog::debug("Binding vertex/index buffers for model id={}", modelId);

			// Set vertex and index buffers for this mesh
			renderPass.setVertexBuffer(0, mesh->getVertexBuffer(), 0, mesh->getVertexCount() * sizeof(Vertex));

			if (mesh->isIndexed())
			{
				renderPass.setIndexBuffer(mesh->getIndexBuffer(), wgpu::IndexFormat::Uint32, 0, mesh->getIndexCount() * sizeof(uint32_t));
			}

			// Update object uniforms (group 2) for the new model instance
			if (item.objectBindGroup)
			{
				renderPass.setBindGroup(2, item.objectBindGroup->getBindGroup(), 0, nullptr);
			}
			else
			{
				// Fallback: Create object uniforms if not provided by collector
				ObjectUniforms objectUniforms{};
				objectUniforms.modelMatrix = item.worldTransform;
				objectUniforms.normalMatrix = glm::transpose(glm::inverse(glm::mat3(item.worldTransform)));

				auto objectBindGroup = m_context->bindGroupFactory().createBindGroup(m_objectBindGroupLayout);
				objectBindGroup->updateBuffer(0, &objectUniforms, sizeof(ObjectUniforms), 0, m_context->getQueue());
				renderPass.setBindGroup(2, objectBindGroup->getBindGroup(), 0, nullptr);

				spdlog::warn("RenderItem missing objectBindGroup, creating temporary one (performance impact)");
			}
		}

		// Get the material for this specific submesh
		const auto &submesh = item.submesh;
		if (!submesh.material.valid())
		{
			spdlog::warn("RenderItem submesh has no valid material, skipping");
			continue;
		}

		uint64_t materialId = submesh.material.id();

		// When material changes: rebind material AND update pipeline
		if (materialId != currentMaterialId)
		{
			currentMaterialId = materialId;

			spdlog::debug("Material changed to id={}, creating GPU material", materialId);

			// Get or create GPU material
			auto gpuMaterial = m_context->materialFactory().createFromHandle(submesh.material);
			if (!gpuMaterial)
			{
				spdlog::warn("Failed to create GPU material, skipping");
				continue;
			}

			// Update GPU resources from CPU (lazy update based on version)
			gpuMaterial->update();

			// Bind material bind group (group 3)
			gpuMaterial->bind(renderPass);

			// Get material's CPU handle to access shader info
			auto materialOpt = submesh.material.get();
			if (!materialOpt.has_value())
			{
				spdlog::warn("Failed to resolve material handle");
				continue;
			}

			auto material = materialOpt.value();

			// Get the model's CPU handle to access mesh
			auto modelOpt = item.modelHandle.get();
			if (!modelOpt.has_value())
			{
				spdlog::warn("Failed to resolve model handle");
				continue;
			}

			auto model = modelOpt.value();
			auto meshHandle = model->getMesh();
			auto meshOpt = meshHandle.get();
			if (!meshOpt.has_value())
			{
				spdlog::warn("Model has no mesh");
				continue;
			}

			auto cpuMesh = meshOpt.value();

			spdlog::debug("Creating pipeline for material id={}", materialId);

			// Get or create render pipeline for this material/mesh combination
			currentPipeline = m_pipelineManager->getOrCreatePipeline(
				cpuMesh,
				material,
				renderPassContext
			);

			if (!currentPipeline || !currentPipeline->isValid())
			{
				spdlog::error("Failed to get/create render pipeline");
				continue;
			}

			// Set the pipeline
			renderPass.setPipeline(currentPipeline->getPipeline());
			spdlog::debug("Pipeline set successfully");
		}

		// Draw this specific submesh
		spdlog::debug("Drawing submesh: indexCount={}, indexOffset={}, indexed={}", 
			submesh.indexCount, submesh.indexOffset, mesh->isIndexed());

		if (mesh->isIndexed())
		{
			renderPass.drawIndexed(submesh.indexCount, 1, submesh.indexOffset, 0, 0);
		}
		else
		{
			renderPass.draw(submesh.indexCount, 1, submesh.indexOffset, 0);
		}
	}

	spdlog::debug("renderItems: Finished processing all items");
}

void Renderer::renderDebugPrimitives(
	wgpu::RenderPassEncoder renderPass,
	const DebugRenderCollector &debugCollector
)
{
	// ToDo: Implement debug primitive rendering (shader, pipeline, bind groups)
}

void Renderer::drawFullscreenQuads(
	wgpu::RenderPassEncoder renderPass,
	const std::vector<std::pair<std::shared_ptr<webgpu::WebGPUTexture>, glm::vec4>> &texturesWithViewports
)
{
	if (texturesWithViewports.empty())
	{
		return;
	}

	if (!m_fullscreenQuadPipeline)
	{
		auto shaderInfo = m_context->shaderRegistry().getShader(shader::default ::FULLSCREEN_QUAD);
		if (!shaderInfo || !shaderInfo->isValid())
		{
			spdlog::error("Fullscreen quad shader not found or invalid");
			return;
		}

		m_fullscreenQuadPipeline = m_context->pipelineFactory().createRenderPipeline(
			shaderInfo,
			shaderInfo,
			m_context->surfaceManager().currentConfig().format,
			wgpu::TextureFormat::Undefined,
			Topology::Triangles,
			wgpu::CullMode::None,
			1
		);

		if (!m_fullscreenQuadPipeline || !m_fullscreenQuadPipeline->isValid())
		{
			spdlog::error("Failed to create fullscreen quad pipeline");
			return;
		}
	}

	auto shaderInfo = m_context->shaderRegistry().getShader(shader::default ::FULLSCREEN_QUAD);
	auto bindGroupLayout = shaderInfo->getBindGroupLayout(0);
	if (!bindGroupLayout)
	{
		spdlog::error("Fullscreen quad shader has no bind group layout at group 0");
		return;
	}

	uint32_t surfaceWidth = m_surfaceTexture->getWidth();
	uint32_t surfaceHeight = m_surfaceTexture->getHeight();

	renderPass.setPipeline(m_fullscreenQuadPipeline->getPipeline());

	// Cache bind groups per texture to avoid creating/releasing every frame
	for (const auto &[texture, viewport] : texturesWithViewports)
	{
		if (!texture)
		{
			continue;
		}

		// Use texture pointer as cache key
		uint64_t cacheKey = reinterpret_cast<uint64_t>(texture.get());

		// Check if we already have a bind group for this texture
		auto it = m_fullscreenQuadBindGroupCache.find(cacheKey);
		std::shared_ptr<webgpu::WebGPUBindGroup> cachedBindGroup;

		if (it == m_fullscreenQuadBindGroupCache.end())
		{
			// Create new bind group for this texture
			std::vector<wgpu::BindGroupEntry> entries;
			entries.reserve(bindGroupLayout->getEntries().size());

			for (const auto &layoutEntry : bindGroupLayout->getEntries())
			{
				wgpu::BindGroupEntry entry{};
				entry.binding = layoutEntry.binding;

				if (layoutEntry.texture.sampleType != wgpu::TextureSampleType::Undefined)
				{
					entry.textureView = texture->getTextureView();
				}
				else if (layoutEntry.sampler.type != wgpu::SamplerBindingType::Undefined)
				{
					entry.sampler = m_fullscreenQuadSampler;
				}

				entries.push_back(entry);
			}

			wgpu::BindGroup rawBindGroup = m_context->bindGroupFactory().createBindGroup(
				bindGroupLayout->getLayout(),
				entries
			);

			// Wrap in WebGPUBindGroup and cache
			cachedBindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
				rawBindGroup,
				bindGroupLayout,
				std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{} // No buffers for texture-only bind groups
			);
			m_fullscreenQuadBindGroupCache[cacheKey] = cachedBindGroup;
		}
		else
		{
			// Reuse cached bind group
			cachedBindGroup = it->second;
		}

		uint32_t vpX = static_cast<uint32_t>(viewport.x * surfaceWidth);
		uint32_t vpY = static_cast<uint32_t>(viewport.y * surfaceHeight);
		uint32_t vpWidth = static_cast<uint32_t>(viewport.z * surfaceWidth);
		uint32_t vpHeight = static_cast<uint32_t>(viewport.w * surfaceHeight);

		renderPass.setViewport(
			static_cast<float>(vpX),
			static_cast<float>(vpY),
			static_cast<float>(vpWidth),
			static_cast<float>(vpHeight),
			0.0f,
			1.0f
		);

		renderPass.setScissorRect(vpX, vpY, vpWidth, vpHeight);

		// Bind and draw (bind group stays alive in cache)
		renderPass.setBindGroup(0, cachedBindGroup->getBindGroup(), 0, nullptr);
		renderPass.draw(3, 1, 0, 0);
	}
}

} // namespace engine::rendering
