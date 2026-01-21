#include "engine/rendering/ShadowPass.h"

#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <spdlog/spdlog.h>

#include "engine/math/Frustum.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/ShadowRequest.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/ShadowUniforms.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering
{

// Cube face directions for shadow cube map rendering (standard OpenGL convention)
struct CubeFace
{
	glm::vec3 target;
	glm::vec3 up;
	uint32_t faceIndex;
};

constexpr CubeFace CUBE_FACES[6] = {
	{glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 0},	 // +X
	{glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 1}, // -X
	{glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 2},	 // +Y
	{glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 3}, // -Y
	{glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f), 4},	 // +Z
	{glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f), 5}	 // -Z
};

ShadowPass::ShadowPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(context)
{
}

bool ShadowPass::initialize()
{
	spdlog::info("Initializing ShadowPass (single-light, texture-agnostic)");

	// Get shadow shaders to extract bind group layouts
	auto shadowShader = m_context->shaderRegistry().getShader(shader::default ::SHADOW);
	if (!shadowShader || !shadowShader->isValid())
	{
		spdlog::error("Shadow shader not found in registry");
		return false;
	}

	auto shadowCubeShader = m_context->shaderRegistry().getShader(shader::default ::SHADOW_CUBE);
	if (!shadowCubeShader || !shadowCubeShader->isValid())
	{
		spdlog::error("Shadow cube shader not found in registry");
		return false;
	}

	// Get bind group layouts from shaders
	m_shadowPass2DBindGroupLayout = shadowShader->getBindGroupLayout(0);		   // Group 0: Shadow uniforms
	m_shadowPassCubeBindGroupLayout = shadowCubeShader->getBindGroupLayout(0); // Group 0: Cube shadow uniforms

	if (!m_shadowPass2DBindGroupLayout || !m_shadowPassCubeBindGroupLayout)
	{
		spdlog::error("Failed to get bind group layouts from shadow shaders");
		return false;
	}

	// Create uniform buffers using buffer factory
	m_shadowPass2DUniformsBuffer = m_context->bufferFactory().createUniformBufferWrapped(
		"Shadow Uniforms Buffer",
		0,
		sizeof(ShadowPass2DUniforms)
	);

	m_shadowPassCubeUniformsBuffer = m_context->bufferFactory().createUniformBufferWrapped(
		"Shadow Cube Uniforms Buffer",
		0,
		sizeof(ShadowPassCubeUniforms)
	);

	// Create reusable bind group for 2D shadows
	{
		std::vector<wgpu::BindGroupEntry> entries(1);
		entries[0].binding = 0;
		entries[0].buffer = m_shadowPass2DUniformsBuffer->getBuffer();
		entries[0].offset = 0;
		entries[0].size = sizeof(ShadowPass2DUniforms);

		wgpu::BindGroupDescriptor bgDesc{};
		bgDesc.layout = m_shadowPass2DBindGroupLayout->getLayout();
		bgDesc.label = "Shadow Pass 2D Bind Group";
		bgDesc.entryCount = entries.size();
		bgDesc.entries = entries.data();
		wgpu::BindGroup bindGroup = m_context->getDevice().createBindGroup(bgDesc);

		m_shadowPass2DBindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
			bindGroup,
			m_shadowPass2DBindGroupLayout,
			std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{m_shadowPass2DUniformsBuffer}
		);
	}

	// Create reusable bind group for cube shadows
	{
		std::vector<wgpu::BindGroupEntry> entries(1);
		entries[0].binding = 0;
		entries[0].buffer = m_shadowPassCubeUniformsBuffer->getBuffer();
		entries[0].offset = 0;
		entries[0].size = sizeof(ShadowPassCubeUniforms);

		wgpu::BindGroupDescriptor bgDesc{};
		bgDesc.layout = m_shadowPassCubeBindGroupLayout->getLayout();
		bgDesc.label = "Shadow Pass Cube Bind Group";
		bgDesc.entryCount = entries.size();
		bgDesc.entries = entries.data();
		wgpu::BindGroup bindGroup = m_context->getDevice().createBindGroup(bgDesc);

		m_shadowPassCubeBindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
			bindGroup,
			m_shadowPassCubeBindGroupLayout,
			std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{m_shadowPassCubeUniformsBuffer}
		);
	}

	// Initialize shadow map resources (textures, sampler, bind group)
	{
		auto shadowLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout("shadowMaps");
		if (shadowLayout == nullptr)
		{
			spdlog::error("Failed to get shadowMaps bind group layout");
			return false;
		}

		m_shadowSampler = m_context->samplerFactory().getShadowComparisonSampler();

		// Create 2D shadow map array
		m_shadow2DArray = m_context->textureFactory().createShadowMap2DArray(
			constants::DEFAULT_SHADOW_MAP_SIZE,
			constants::MAX_SHADOW_MAPS_2D
		);

		// Create cube shadow map array
		m_shadowCubeArray = m_context->textureFactory().createShadowMapCubeArray(
			constants::DEFAULT_CUBE_SHADOW_MAP_SIZE,
			constants::MAX_SHADOW_MAPS_CUBE
		);

		std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> resources = {
			{{4, 0}, webgpu::BindGroupResource(m_shadowSampler)},
			{{4, 1}, webgpu::BindGroupResource(m_shadow2DArray)},
			{{4, 2}, webgpu::BindGroupResource(m_shadowCubeArray)}
		};

		// Create bind group (sampler + textures)
		m_shadowBindGroup = m_context->bindGroupFactory().createBindGroup(
			shadowLayout,
			resources,
			nullptr,
			"ShadowMaps BindGroup"
		);

		spdlog::info("Shadow map resources initialized (2D array: {}x{}, Cube array: {}x{})",
			constants::DEFAULT_SHADOW_MAP_SIZE, constants::MAX_SHADOW_MAPS_2D,
			constants::DEFAULT_CUBE_SHADOW_MAP_SIZE, constants::MAX_SHADOW_MAPS_CUBE
		);
	}

	spdlog::info("ShadowPass initialized successfully");
	return true;
}

ShadowUniform ShadowPass::computeShadowUniform(const ShadowRequest &request)
{
	ShadowUniform shadowUniform{};
	const Light *light = request.light;

	// Set common properties based on light type
	std::visit(
		[&](auto &&specificLight)
		{
			using T = std::decay_t<decltype(specificLight)>;

			if constexpr (std::is_same_v<T, DirectionalLight>)
			{
				// Compute directional light shadow matrix
				glm::vec3 dir = glm::normalize(glm::vec3(light->getTransform() * glm::vec4(specificLight.direction, 0.0f)));
				glm::vec3 pos = -dir * specificLight.range;
				
				glm::mat4 viewMatrix = glm::lookAt(pos, pos + dir, glm::vec3(0, 1, 0));
				float r = specificLight.range;
				glm::mat4 projectionMatrix = glm::ortho(-r, r, -r, r, -specificLight.range, specificLight.range * 2.0f);
				
				shadowUniform.viewProj = projectionMatrix * viewMatrix;
				shadowUniform.bias = specificLight.shadowBias;
				shadowUniform.normalBias = specificLight.shadowNormalBias;
				shadowUniform.texelSize = 1.0f / static_cast<float>(specificLight.shadowMapSize);
				shadowUniform.pcfKernel = specificLight.shadowPCFKernel;
				shadowUniform.shadowType = 0; // 2D shadow
			}
			else if constexpr (std::is_same_v<T, SpotLight>)
			{
				// Compute spot light shadow matrix
				glm::vec3 pos = glm::vec3(light->getTransform() * glm::vec4(specificLight.position, 1.0f));
				// Extract forward direction from transform (spotlight points in -Z direction)
				glm::vec3 dir = glm::normalize(glm::vec3(light->getTransform() * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
				
				glm::mat4 viewMatrix = glm::lookAt(pos, pos + dir, glm::vec3(0, 1, 0));
				glm::mat4 projectionMatrix = glm::perspective(specificLight.spotAngle * 2.0f, 1.0f, 0.1f, specificLight.range);
				
				shadowUniform.viewProj = projectionMatrix * viewMatrix;
				shadowUniform.bias = specificLight.shadowBias;
				shadowUniform.normalBias = specificLight.shadowNormalBias;
				shadowUniform.texelSize = 1.0f / static_cast<float>(specificLight.shadowMapSize);
				shadowUniform.pcfKernel = specificLight.shadowPCFKernel;
				shadowUniform.shadowType = 0; // 2D shadow
			}
			else if constexpr (std::is_same_v<T, PointLight>)
			{
				// Point light - store position for cube map rendering
				shadowUniform.lightPos = glm::vec3(light->getTransform() * glm::vec4(specificLight.position, 1.0f));
				shadowUniform.bias = specificLight.shadowBias;
				shadowUniform.texelSize = 1.0f / static_cast<float>(specificLight.shadowMapSize);
				shadowUniform.pcfKernel = specificLight.shadowPCFKernel;
				shadowUniform.shadowType = 1; // Cube shadow
			}
		},
		light->getData()
	);

	shadowUniform.textureIndex = request.textureIndex;
	return shadowUniform;
}

void ShadowPass::render(FrameCache &frameCache)
{
	if (!m_collector)
	{
		spdlog::error("ShadowPass::render() called without setting collector");
		return;
	}

	if (frameCache.shadowRequests.empty())
		return;

	// Compute shadow uniforms from requests (this is where matrices are computed per-camera)
	frameCache.shadowUniforms.clear();
	frameCache.shadowUniforms.reserve(frameCache.shadowRequests.size());

	for (const auto &request : frameCache.shadowRequests)
	{
		frameCache.shadowUniforms.push_back(computeShadowUniform(request));
	}

	// Render shadow maps for each request
	for (size_t i = 0; i < frameCache.shadowRequests.size(); ++i)
	{
		const auto &request = frameCache.shadowRequests[i];
		const auto &shadowUniform = frameCache.shadowUniforms[i];

		// Determine visible objects for this light
		std::vector<size_t> visibleIndices;
		if (request.type == ShadowType::PointCube)
		{
			// Point light - extract by sphere
			visibleIndices = m_collector->extractForPointLight(
				shadowUniform.lightPos,
				request.light->asPoint().range
			);
		}
		else // Directional2D or Spot2D
		{
			// Extract by frustum
			visibleIndices = m_collector->extractForLightFrustum(
				engine::math::Frustum::fromViewProjection(shadowUniform.viewProj)
			);

		}

		// Prepare GPU resources for visible items
		frameCache.prepareGPUResources(m_context, *m_collector, visibleIndices);

		// Render shadow map based on type
		if (request.type == ShadowType::PointCube)
		{
			renderShadowCube(
				frameCache.gpuRenderItems,
				visibleIndices,
				m_shadowCubeArray,
				request.textureIndex,
				shadowUniform.lightPos,
				request.light->asPoint().range
			);
		}
		else // Directional2D or Spot2D
		{
			renderShadow2D(
				frameCache.gpuRenderItems,
				visibleIndices,
				m_shadow2DArray,
				request.textureIndex,
				shadowUniform.viewProj
			);
		}
	}

	// Update GPU shadow uniform buffer
	if (!frameCache.shadowUniforms.empty())
	{
		m_shadowBindGroup->updateBuffer(
			3,
			frameCache.shadowUniforms.data(),
			frameCache.shadowUniforms.size() * sizeof(ShadowUniform),
			0,
			m_context->getQueue()
		);
	}
}

void ShadowPass::renderShadow2D(
	const std::vector<std::optional<RenderItemGPU>> &gpuItems,
	const std::vector<size_t> &indicesToRender, // only items visible to this light
	const std::shared_ptr<webgpu::WebGPUTexture> shadowTexture,
	uint32_t arrayLayer,
	const glm::mat4 &lightViewProjection
)
{
	// Prepare shadow uniforms
	ShadowPass2DUniforms shadowUniforms;
	shadowUniforms.lightViewProjectionMatrix = lightViewProjection;

	uint32_t shadowMapSize = shadowTexture->getWidth();
	// Update uniforms using the reusable bind group
	m_shadowPass2DBindGroup->updateBuffer(0, &shadowUniforms, sizeof(ShadowPass2DUniforms), 0, m_context->getQueue());

	auto shadowDebugingTexture = m_context->textureFactory().createRenderTarget(
		-1 - arrayLayer, // Unique negative ID for debugging
		shadowMapSize,
		shadowMapSize,
		wgpu::TextureFormat::RGBA8Unorm
	);

	// Create render pass using factory
	// auto renderPassContext = m_context->renderPassFactory().createDepthOnly(shadowTexture, arrayLayer);
	// ToDo: Remove Debug
	auto renderPassContext = m_context->renderPassFactory().create(
		shadowDebugingTexture,
		shadowTexture,
		ClearFlags::Depth | ClearFlags::SolidColor,
		glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), // Debug: Clear to green
		-1,
		arrayLayer
	);
	// Create command encoder
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "Shadow 2D Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// Begin render pass
	wgpu::RenderPassEncoder renderPass = renderPassContext->begin(encoder);

	renderPass.setViewport(0.0f, 0.0f, static_cast<float>(shadowMapSize), static_cast<float>(shadowMapSize), 0.0f, 1.0f);
	renderPass.setScissorRect(0, 0, shadowMapSize, shadowMapSize);

	// Bind shadow uniforms (group 0)
	renderPass.setBindGroup(0, m_shadowPass2DBindGroup->getBindGroup(), 0, nullptr);

	// Render all items
	renderItems(renderPass, gpuItems, indicesToRender, false);

	// End render pass and submit
	renderPassContext->end(renderPass);

	// Submit commands
	m_context->submitCommandEncoder(encoder, "Shadow 2D Commands");
}

void ShadowPass::renderShadowCube(
	const std::vector<std::optional<RenderItemGPU>> &items,
	const std::vector<size_t> &indicesToRender,
	const std::shared_ptr<webgpu::WebGPUTexture> shadowTexture,
	uint32_t cubeIndex,
	const glm::vec3 &lightPosition,
	float farPlane
)
{
	// Perspective projection (90° FOV)
	glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);
	// Create a single command encoder for all faces
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = ("Shadow Cube Encoder " + std::to_string(cubeIndex)).c_str();
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	for (const auto &face : CUBE_FACES)
	{
		glm::mat4 view = glm::lookAt(
			lightPosition,
			lightPosition + face.target,
			face.up
		);
		glm::mat4 lightVP = projection * view;

		// Update uniforms
		ShadowPassCubeUniforms shadowCubeUniforms;
		shadowCubeUniforms.lightPosition = lightPosition;
		shadowCubeUniforms.farPlane = farPlane;
		m_shadowPassCubeBindGroup->updateBuffer(
			0,
			&shadowCubeUniforms,
			sizeof(ShadowPassCubeUniforms),
			0,
			m_context->getQueue()
		);

		uint32_t layerIndex = cubeIndex * 6 + face.faceIndex;
		std::string faceLabel = "Shadow Cube Face " + std::to_string(face.faceIndex);
		
		auto renderPassContext = m_context->renderPassFactory().createDepthOnly(shadowTexture, layerIndex);

		wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

		uint32_t cubeMapSize = shadowTexture->getWidth();
		renderPass.setViewport(0.0f, 0.0f, static_cast<float>(cubeMapSize), static_cast<float>(cubeMapSize), 0.0f, 1.0f);
		renderPass.setScissorRect(0, 0, cubeMapSize, cubeMapSize);

		renderPass.setBindGroup(0, m_shadowPassCubeBindGroup->getBindGroup(), 0, nullptr);

		// Render all items
		renderItems(renderPass, items, indicesToRender, true);

		renderPass.end();
		renderPass.release();
	}

	// Submit all faces in one go
	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = ("Shadow Cube Commands " + std::to_string(cubeIndex)).c_str();
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);

	m_context->getDevice().getQueue().submit(1, &commands);

	commands.release();
	encoder.release();
}

std::shared_ptr<webgpu::WebGPUPipeline> ShadowPass::getOrCreatePipeline(
	engine::rendering::Topology::Type topology,
	bool isCubeShadow
)
{
	// Use topology type as cache key (NOT mesh pointer)
	int cacheKey = static_cast<int>(topology);

	// Use different cache for cube shadows
	auto &cache = isCubeShadow ? m_cubePipelineCache : m_pipelineCache;

	// Check cache and validate weak_ptr
	auto it = cache.find(cacheKey);
	if (it != cache.end())
	{
		auto pipeline = it->second.lock(); // Try to convert weak_ptr to shared_ptr
		if (pipeline && pipeline->isValid())
		{
			return pipeline; // Cache hit, pipeline still valid
		}
		else
		{
			// Cache miss: pipeline was released, remove stale entry
			cache.erase(it);
		}
	}

	// Get appropriate shadow shader
	auto shaderName = isCubeShadow ? shader::default ::SHADOW_CUBE : shader::default ::SHADOW;
	auto shadowShader = m_context->shaderRegistry().getShader(shaderName);
	if (!shadowShader || !shadowShader->isValid())
	{
		spdlog::error("Shadow shader '{}' not found or invalid", shaderName);
		return nullptr;
	}

	// Create pipeline for depth-only rendering
	auto pipeline = m_context->pipelineManager().getOrCreatePipeline(
		shadowShader,					  // Shader
		wgpu::TextureFormat::RGBA8Unorm,	  // Color format (debug) // ToDo: Remove Debug
		wgpu::TextureFormat::Depth32Float, // Depth format
		topology,						  // Topology from mesh
		wgpu::CullMode::None,			  // Cull mode
		1								  // Sample count
	);

	if (!pipeline || !pipeline->isValid())
	{
		spdlog::error("Failed to create shadow pipeline");
		return nullptr;
	}

	// Cache pipeline as weak_ptr (by topology type, not per-mesh instance)
	cache[cacheKey] = pipeline; // implicit conversion: shared_ptr to weak_ptr

	return pipeline;
}

void ShadowPass::cleanup()
{
	m_pipelineCache.clear();
	m_cubePipelineCache.clear();
}

void ShadowPass::renderItems(
	wgpu::RenderPassEncoder &renderPass,
	const std::vector<std::optional<RenderItemGPU>> &gpuItems,
	const std::vector<size_t> &indicesToRender,
	bool isCubeShadow
)
{
	const webgpu::WebGPUPipeline *currentPipeline = nullptr;
	const webgpu::WebGPUMesh *currentMesh = nullptr;

	int renderedCount = 0;

	for (const auto &index : indicesToRender)
	{
		if (index >= gpuItems.size())
			continue;

		const auto &gpuItemOpt = gpuItems[index];
		if (!gpuItemOpt.has_value())
			continue;
		const auto &gpuItem = gpuItemOpt.value();
		if (!gpuItem.gpuMesh)
			continue;

		auto meshOpt = gpuItem.gpuMesh->getCPUHandle().get();
		if (!meshOpt.has_value())
			continue;

		auto mesh = meshOpt.value();

		// Get or create pipeline based on topology
		auto pipeline = getOrCreatePipeline(mesh->getTopology(), isCubeShadow);
		if (!pipeline || !pipeline->isValid())
			continue;

		// Bind pipeline if changed
		if (pipeline.get() != currentPipeline)
		{
			currentPipeline = pipeline.get();
			renderPass.setPipeline(currentPipeline->getPipeline());
		}

		renderPass.setBindGroup(1, gpuItem.objectBindGroup->getBindGroup(), 0, nullptr);

		// Bind vertex/index buffers if mesh changed
		if (gpuItem.gpuMesh != currentMesh)
		{
			currentMesh = gpuItem.gpuMesh;
			gpuItem.gpuMesh->bindBuffers(renderPass, currentPipeline->getVertexLayout());
		}

		// Draw submesh
		if (gpuItem.gpuMesh->isIndexed())
		{
			renderPass.drawIndexed(gpuItem.submesh.indexCount, 1, gpuItem.submesh.indexOffset, 0, 0);
		}
		else
		{
			renderPass.draw(gpuItem.submesh.indexCount, 1, gpuItem.submesh.indexOffset, 0);
		}

		renderedCount++;
	}

	if (renderedCount > 0)
	{
		spdlog::debug("Shadow pass rendered {} items", renderedCount);
	}
	else
	{
		spdlog::warn("Shadow pass rendered 0 items!");
	}
}

} // namespace engine::rendering
