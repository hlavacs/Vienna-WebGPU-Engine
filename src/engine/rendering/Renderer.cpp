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
	FrameCache &frameCache,
	const RenderCollector &renderCollector,
	std::function<void(wgpu::RenderPassEncoder)> uiCallback
)
{
	if (frameCache.renderTargets.empty())
	{
		spdlog::warn("renderFrame called with no render targets");
		return false;
	}

	startFrame();
	renderShadowMaps(renderCollector);

	std::vector<uint64_t> cameraIds;

	for (const auto &target : frameCache.renderTargets)
	{
		renderCameraInternal(renderCollector, target, frameCache.time);
		cameraIds.push_back(target.cameraId);
	}

	compositeTexturesToSurface(cameraIds, uiCallback);

	// Clear frame cache after rendering
	frameCache.clear();

	return true;
}

void Renderer::startFrame()
{
	m_surfaceTexture = m_context->surfaceManager().acquireNextTexture();
	m_gpuRenderItems.clear(); // Clear GPU render items at the start of each frame
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
	auto shadowLights = collector.extractLightUniformsWithShadows(
		constants::MAX_SHADOW_MAPS_2D,
		constants::MAX_SHADOW_MAPS_CUBE
	);

	if (shadowLights.empty())
		return;

	// Get the actual Light objects to access shadow configuration
	const auto &lights = collector.getLights();

	// Prepare buffers if necessary
	std::vector<Shadow2D> shadow2DData(constants::MAX_SHADOW_MAPS_2D);
	std::vector<ShadowCube> shadowCubeData(constants::MAX_SHADOW_MAPS_CUBE);

	for (size_t lightIdx = 0; lightIdx < shadowLights.size(); ++lightIdx)
	{
		const auto &lightUniform = shadowLights[lightIdx];
		const auto &light = lights[lightIdx];
		
		if (lightUniform.shadowIndex < 0) // No need to render shadow map if index is invalid
			continue;

		std::vector<size_t> visibleIndices;
		glm::mat4 lightVP(1.0f);
		glm::vec3 position = glm::vec3(lightUniform.transform[3]);
		glm::vec3 direction = -glm::normalize(glm::vec3(lightUniform.transform * glm::vec4(0, 0, -1, 0)));
		glm::vec3 up = glm::abs(glm::dot(direction, glm::vec3(0, 1, 0))) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

		switch (lightUniform.light_type)
		{
		case 1: // Directional
		{
			const auto &dirLight = light.asDirectional();
			float nearPlane = -dirLight.range;
			float farPlane = dirLight.range;
			float halfWidth = dirLight.range;
			float halfHeight = dirLight.range;
			
			lightVP = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane)
					  * glm::lookAt(position - direction * farPlane / 2.0f, position, up);
			
			visibleIndices = collector.extractForLightFrustum(engine::math::Frustum::fromViewProjection(lightVP));
			
			auto &shadow = shadow2DData[lightUniform.shadowIndex];
			shadow.lightViewProjection = lightVP;
			shadow.bias = dirLight.shadowBias;
			shadow.normalBias = dirLight.shadowNormalBias;
			shadow.texelSize = 1.0f / static_cast<float>(dirLight.shadowMapSize);
			shadow.pcfKernel = dirLight.shadowPCFKernel;
			break;
		}
		case 2: // Point
		{
			const auto &pointLight = light.asPoint();
			visibleIndices = collector.extractForPointLight(position, pointLight.range);
			
			auto &shadow = shadowCubeData[lightUniform.shadowIndex];
			shadow.lightPosition = position;
			shadow.bias = pointLight.shadowBias;
			shadow.texelSize = 1.0f / static_cast<float>(pointLight.shadowMapSize);
			shadow.pcfKernel = pointLight.shadowPCFKernel;
			break;
		}
		case 3: // Spot
		{
			const auto &spotLight = light.asSpot();
			float nearPlane = 0.01f;
			float farPlane = spotLight.range;
			float aspect = 1.0f;
			
			lightVP = glm::perspective(spotLight.spotAngle * 2.0f, aspect, nearPlane, farPlane)
					  * glm::lookAt(position, position + direction, up);
			
			visibleIndices = collector.extractForLightFrustum(engine::math::Frustum::fromViewProjection(lightVP));
			
			auto &shadow = shadow2DData[lightUniform.shadowIndex];
			shadow.lightViewProjection = lightVP;
			shadow.bias = spotLight.shadowBias;
			shadow.normalBias = spotLight.shadowNormalBias;
			shadow.texelSize = 1.0f / static_cast<float>(spotLight.shadowMapSize);
			shadow.pcfKernel = spotLight.shadowPCFKernel;
			break;
		}
		default:
			continue;
		}

		auto gpuItems = prepareGPUResources(collector, visibleIndices);

		if (lightUniform.light_type == 2)
			m_shadowPass->renderShadowCube(gpuItems, visibleIndices, m_shadowResources.shadowCubeArray, static_cast<uint32_t>(lightUniform.shadowIndex), position, lightUniform.range);
		else
			m_shadowPass->renderShadow2D(gpuItems, visibleIndices, m_shadowResources.shadow2DArray, static_cast<uint32_t>(lightUniform.shadowIndex), lightVP);
	}

	// After all lights, write the data to GPU buffers
	if (!shadow2DData.empty())
		m_shadowResources.bindGroup->updateBuffer(3, shadow2DData.data(), shadow2DData.size() * sizeof(Shadow2D), 0, m_context->getQueue());
	if (!shadowCubeData.empty())
		m_shadowResources.bindGroup->updateBuffer(4, shadowCubeData.data(), shadowCubeData.size() * sizeof(ShadowCube), 0, m_context->getQueue());

	// Then give the bind group to mesh pass
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
	auto gpuItems = prepareGPUResources(collector, visibleIndices);

	// Extract light uniforms from Light objects
	auto lightUniforms = collector.extractLightUniformsWithShadows(
		constants::MAX_SHADOW_MAPS_2D,
		constants::MAX_SHADOW_MAPS_CUBE
	);

	// Render using MeshPass
	spdlog::debug("Rendering {} GPU items using MeshPass", gpuItems.size());
	m_meshPass->render(gpuItems, visibleIndices, lightUniforms, renderPassContext, frameUniforms, renderTargetId);

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

	std::vector<RenderTarget> renderTargetsToComposite;
	renderTargetsToComposite.reserve(targetIds.size());

	for (uint64_t targetId : targetIds)
	{
		auto it = m_renderTargets.find(targetId);
		if (it != m_renderTargets.end() && it->second.gpuTexture)
		{
			/* it->second.gpuTexture = m_context->textureFactory().createRenderTarget(
				-1,
				2048,
				2048,
				wgpu::TextureFormat::RGBA8Unorm
			); */
			renderTargetsToComposite.push_back(it->second);
		}
	}

	// Composite all textures in one render pass
	if (!renderTargetsToComposite.empty())
	{
		auto renderPassContext = m_context->renderPassFactory().create(m_surfaceTexture);
		m_compositePass->render(renderPassContext, renderTargetsToComposite);
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

std::vector<std::optional<RenderItemGPU>> Renderer::prepareGPUResources(
	const RenderCollector &collector,
	const std::vector<size_t> &indicesToPrepare
)
{
	// Make sure the local GPU item cache matches CPU items
	if (m_gpuRenderItems.size() != collector.getRenderItems().size())
		m_gpuRenderItems.resize(collector.getRenderItems().size());

	auto objectBindGroupLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("objectUniforms");
	if (!objectBindGroupLayout)
	{
		spdlog::error("Failed to get objectUniforms bind group layout");
		return m_gpuRenderItems;
	}

	const auto &cpuItems = collector.getRenderItems();
	for (size_t idx : indicesToPrepare)
	{
		// Skip if already prepared
		if (m_gpuRenderItems[idx].has_value())
			continue;

		const auto &cpuItem = cpuItems[idx];

		// Create GPU model (factory caches internally)
		auto gpuModel = m_context->modelFactory().createFromHandle(cpuItem.modelHandle);
		if (!gpuModel)
		{
			spdlog::warn("Failed to create GPU model for handle {}", cpuItem.modelHandle.id());
			continue;
		}

		gpuModel->syncIfNeeded();

		// Get GPU mesh
		auto gpuMesh = gpuModel->getMesh().get();
		if (!gpuMesh)
		{
			spdlog::warn("Failed to get GPU mesh from model {}", cpuItem.modelHandle.id());
			continue;
		}

		gpuMesh->syncIfNeeded();

		// Get GPU material
		auto materialHandle = cpuItem.submesh.material;
		auto gpuMaterial = m_context->materialFactory().createFromHandle(materialHandle);
		if (!gpuMaterial)
		{
			spdlog::warn("Failed to create GPU material for submesh");
			continue;
		}

		gpuMaterial->syncIfNeeded();

		// Get or create object bind group
		std::shared_ptr<webgpu::WebGPUBindGroup> objectBindGroup;
		auto it = m_objectBindGroupCache.find(cpuItem.objectID);
		if (it != m_objectBindGroupCache.end())
		{
			objectBindGroup = it->second;
		}
		else
		{
			objectBindGroup = m_context->bindGroupFactory().createBindGroup(objectBindGroupLayout);
			if (cpuItem.objectID != 0)
				m_objectBindGroupCache[cpuItem.objectID] = objectBindGroup;
		}

		auto objectUniforms = ObjectUniforms{cpuItem.worldTransform, glm::inverseTranspose(cpuItem.worldTransform)};
		objectBindGroup->updateBuffer(
			0,
			&objectUniforms,
			sizeof(ObjectUniforms),
			0,
			m_context->getQueue()
		);

		// Fill GPU render item
		RenderItemGPU gpuItem;
		gpuItem.gpuModel = gpuModel;
		gpuItem.gpuMesh = gpuMesh;
		gpuItem.gpuMaterial = gpuMaterial;
		gpuItem.objectBindGroup = objectBindGroup;
		gpuItem.submesh = cpuItem.submesh;
		gpuItem.worldTransform = cpuItem.worldTransform;
		gpuItem.renderLayer = cpuItem.renderLayer;
		gpuItem.objectID = cpuItem.objectID;

		m_gpuRenderItems[idx] = gpuItem;
	}

	spdlog::debug("Prepared GPU resources: {}/{} items", std::count_if(m_gpuRenderItems.begin(), m_gpuRenderItems.end(), [](auto &i)
																	   { return i.has_value(); }),
				  collector.getRenderItems().size());

	return m_gpuRenderItems;
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
