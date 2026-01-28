#include "engine/rendering/ShadowPass.h"

#include <cmath>
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
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/ShadowRequest.h"
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
	{glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0}, // -X (right)
	{glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1},	// +X (left)
	{glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 2}, // +Y (up)
	{glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 3}, // -Y (down)
	{glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), 4},	// +Z (forward)
	{glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f), 5}	// -Z (backward)
};

ShadowPass::ShadowPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(context)
{
}

bool ShadowPass::initialize()
{
	spdlog::info("Initializing ShadowPass");

	// Get shadow shaders to extract bind group layouts
	auto shadowShader = m_context->shaderRegistry().getShader(shader::defaults::SHADOW_PASS_2D);
	if (!shadowShader || !shadowShader->isValid())
	{
		spdlog::error("Shadow shader not found in registry");
		return false;
	}

	auto shadowCubeShader = m_context->shaderRegistry().getShader(shader::defaults ::SHADOW_PASS_CUBE);
	if (!shadowCubeShader || !shadowCubeShader->isValid())
	{
		spdlog::error("Shadow cube shader not found in registry");
		return false;
	}

	// Get bind group layouts from shaders
	m_shadowPass2DBindGroupLayout = shadowShader->getBindGroupLayout(bindgroup::defaults::SHADOW_PASS_2D);
	m_shadowPassCubeBindGroupLayout = shadowCubeShader->getBindGroupLayout(bindgroup::defaults::SHADOW_PASS_CUBE);

	if (!m_shadowPass2DBindGroupLayout || !m_shadowPassCubeBindGroupLayout)
	{
		spdlog::error("Failed to get bind group layouts from shadow shaders");
		return false;
	}

	m_shadowPass2DBindGroup = m_context->bindGroupFactory().createBindGroup(
		m_shadowPass2DBindGroupLayout,
		{},
		nullptr,
		"Shadow Pass 2D Bind Group"
	);

	for (int i = 0; i < 6; ++i)
	{
		m_shadowPassCubeBindGroup[i] = m_context->bindGroupFactory().createBindGroup(
			m_shadowPassCubeBindGroupLayout,
			{},
			nullptr,
			"Shadow Pass Cube Bind Group"
		);
	}

	// Initialize shadow map resources (textures, sampler, bind group)
	{
		auto shadowLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout(bindgroup::defaults::SHADOW);
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

		spdlog::info("Shadow map resources initialized (2D array: {}x{}, Cube array: {}x{})", constants::DEFAULT_SHADOW_MAP_SIZE, constants::MAX_SHADOW_MAPS_2D, constants::DEFAULT_CUBE_SHADOW_MAP_SIZE, constants::MAX_SHADOW_MAPS_CUBE);
	}

	DEBUG_SHADOW_2D_ARRAY = m_context->textureFactory().createShadowMap2DArray(
		constants::DEFAULT_SHADOW_MAP_SIZE,
		constants::MAX_SHADOW_MAPS_2D,
		wgpu::TextureFormat::RGBA8Unorm
	);

	DEBUG_SHADOW_CUBE_ARRAY = m_context->textureFactory().createShadowMapCubeArray(
		constants::DEFAULT_CUBE_SHADOW_MAP_SIZE,
		constants::MAX_SHADOW_MAPS_CUBE,
		wgpu::TextureFormat::RGBA8Unorm
	);

	spdlog::info("ShadowPass initialized successfully");
	return true;
}

std::vector<ShadowUniform> ShadowPass::computeShadowUniforms(
	const ShadowRequest &request,
	const engine::rendering::RenderTarget &renderTarget,
	float splitLambda
)
{
	std::vector<ShadowUniform> result;
	const Light *light = request.light;
	auto cameraNear = renderTarget.nearPlane;
	auto cameraFar = renderTarget.farPlane;

	std::visit([&](auto &&specificLight)
			   {
		using T = std::decay_t<decltype(specificLight)>;

		if constexpr (std::is_same_v<T, DirectionalLight>) {
			glm::vec3 dir = glm::normalize(light->getTransform()[2]);
			glm::mat4 lightView = glm::lookAt(-dir * specificLight.range, glm::vec3(0), glm::vec3(0, 1, 0));

			if (request.cascadeCount > 1) {
				auto cascades = engine::math::Frustum::computeCascades(
					renderTarget.frustum, renderTarget.viewMatrix, lightView,
					cameraNear, cameraFar, specificLight.range, request.cascadeCount, splitLambda
				);

				for (uint32_t i = 0; i < request.cascadeCount; ++i) {
					ShadowUniform u;
					u.viewProj = cascades[i].viewProj;
					u.near = cascades[i].near;
					u.far = cascades[i].far;
					u.cascadeSplit = cascades[i].cascadeSplit;
					
					u.bias = specificLight.shadowBias;
					u.normalBias = specificLight.shadowNormalBias;
					u.texelSize = 1.0f / static_cast<float>(specificLight.shadowMapSize);
					u.pcfKernel = specificLight.shadowPCFKernel;
					u.shadowType = 0;
					u.textureIndex = request.textureIndexStart + i;
					result.push_back(u);
				}
			} else {
				ShadowUniform u;
				float r = specificLight.range;
				u.viewProj = glm::ortho(-r, r, -r, r, -specificLight.range, specificLight.range * 2.0f) * lightView;
				u.near = cameraNear;
				u.far = cameraFar;
				u.cascadeSplit = cameraFar;
				u.bias = specificLight.shadowBias;
				u.normalBias = specificLight.shadowNormalBias;
				u.texelSize = 1.0f / static_cast<float>(specificLight.shadowMapSize);
				u.pcfKernel = specificLight.shadowPCFKernel;
				u.shadowType = 0;
				u.textureIndex = request.textureIndexStart;
				result.push_back(u);
			}
		}
		else if constexpr (std::is_same_v<T, SpotLight>) {
			ShadowUniform u;
			glm::vec3 pos = light->getTransform()[3];
			glm::vec3 dir = glm::normalize(light->getTransform()[2]);
			u.viewProj = glm::perspective(specificLight.spotAngle * 2.0f, 1.0f, 0.1f, specificLight.range)
				* glm::lookAt(pos, pos + dir, glm::vec3(0, 1, 0));
			u.near = 0.1f;
			u.far = specificLight.range;
			u.cascadeSplit = specificLight.range;
			u.bias = specificLight.shadowBias;
			u.normalBias = specificLight.shadowNormalBias;
			u.texelSize = 1.0f / static_cast<float>(specificLight.shadowMapSize);
			u.pcfKernel = specificLight.shadowPCFKernel;
			u.shadowType = 0;
			u.textureIndex = request.textureIndexStart;
			result.push_back(u);
		}
		else if constexpr (std::is_same_v<T, PointLight>) {
			ShadowUniform u;
			u.lightPos = light->getTransform()[3];
			u.near = 0.1f;
			u.far = specificLight.range;
			u.cascadeSplit = specificLight.range;
			u.bias = specificLight.shadowBias;
			u.normalBias = specificLight.shadowNormalBias;
			u.texelSize = 1.0f / static_cast<float>(specificLight.shadowMapSize);
			u.pcfKernel = specificLight.shadowPCFKernel;
			u.shadowType = 1;
			u.textureIndex = request.textureIndexStart;
			result.push_back(u);
		} },
			   light->getData());

	return result;
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

	const auto &renderTarget = frameCache.renderTargets[m_cameraId];
	frameCache.shadowUniforms.clear();

	// Count total shadow uniforms needed (accounting for CSM cascades)
	size_t totalUniforms = 0;
	for (const auto &request : frameCache.shadowRequests)
	{
		totalUniforms += request.cascadeCount;
	}
	frameCache.shadowUniforms.reserve(totalUniforms);

	// Create shadow uniforms (one per cascade for CSM)
	for (const auto &request : frameCache.shadowRequests)
	{
		auto splitLambda = 0.5f;
		if (request.type == ShadowType::Directional)
		{
			const auto &dirLight = request.light->asDirectional();
			splitLambda = dirLight.splitLambda;
		}
		auto shadowUniforms = computeShadowUniforms(request, renderTarget, splitLambda);
		for(const auto &u : shadowUniforms)
		{
			frameCache.shadowUniforms.push_back(u);
		}
	}

	// Render shadow maps for each request
	size_t uniformIndex = 0;
	for (const auto &request : frameCache.shadowRequests)
	{
		if (request.type == ShadowType::PointCube)
		{
			// Point light - single cube shadow
			const auto &shadowUniform = frameCache.shadowUniforms[uniformIndex];
			uniformIndex++;

			// Determine visible objects for this light
			std::vector<size_t> visibleIndices = m_collector->extractForPointLight(
				shadowUniform.lightPos,
				request.light->asPoint().range
			);

			// Prepare GPU resources for visible items
			frameCache.prepareGPUResources(m_context, *m_collector, visibleIndices);

			renderShadowCube(
				frameCache.gpuRenderItems,
				visibleIndices,
				m_shadowCubeArray,
				request.textureIndexStart,
				shadowUniform.lightPos,
				request.light->asPoint().range
			);
		}
		else if (request.type == ShadowType::Directional && request.cascadeCount > 1)
		{
			// CSM: Render each cascade
			for (uint32_t cascadeIdx = 0; cascadeIdx < request.cascadeCount; ++cascadeIdx)
			{
				const auto &shadowUniform = frameCache.shadowUniforms[uniformIndex];
				uniformIndex++;

				// Extract visible objects for this cascade frustum
				std::vector<size_t> visibleIndices = m_collector->extractForLightFrustum(
					engine::math::Frustum::fromViewProjection(shadowUniform.viewProj)
				);

				// Prepare GPU resources for visible items
				frameCache.prepareGPUResources(m_context, *m_collector, visibleIndices);

				renderShadow2D(
					frameCache.gpuRenderItems,
					visibleIndices,
					m_shadow2DArray,
					request.textureIndexStart + cascadeIdx,
					shadowUniform.viewProj,
					shadowUniform.lightPos,
					request.light->asDirectional().range
				);
			}
		}
		else
		{
			// Single 2D shadow (spot or single directional)
			const auto &shadowUniform = frameCache.shadowUniforms[uniformIndex];
			uniformIndex++;

			// Extract by frustum
			std::vector<size_t> visibleIndices = m_collector->extractForLightFrustum(
				engine::math::Frustum::fromViewProjection(shadowUniform.viewProj)
			);

			// Prepare GPU resources for visible items
			frameCache.prepareGPUResources(m_context, *m_collector, visibleIndices);

			renderShadow2D(
				frameCache.gpuRenderItems,
				visibleIndices,
				m_shadow2DArray,
				request.textureIndexStart,
				shadowUniform.viewProj,
				shadowUniform.lightPos,
				request.light->asSpot().range
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
	const glm::mat4 &lightViewProjection,
	const glm::vec3 &lightPosition,
	float farPlane
)
{
	// Prepare shadow uniforms
	ShadowPass2DUniforms shadowUniforms;
	shadowUniforms.lightViewProjectionMatrix = lightViewProjection;
	shadowUniforms.lightPos = lightPosition; // Extract light position from matrix
	shadowUniforms.farPlane = farPlane;		 // Extract light position from matrix

	uint32_t shadowMapSize = shadowTexture->getWidth();
	// Update uniforms using the reusable bind group
	m_shadowPass2DBindGroup->updateBuffer(0, &shadowUniforms, sizeof(ShadowPass2DUniforms), 0, m_context->getQueue());

	std::shared_ptr<webgpu::WebGPURenderPassContext> renderPassContext = nullptr;

	// Create render pass using factory
	if (m_isDebugMode)
	{
		// Debug: Clear to solid color for visualization
		renderPassContext = m_context->renderPassFactory().create(
			DEBUG_SHADOW_2D_ARRAY,
			shadowTexture,
			ClearFlags::SolidColor | ClearFlags::Depth,
			glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), // Clear to black
			arrayLayer,
			arrayLayer
		);
	}
	else
	{
		// Normal: Clear depth only
		renderPassContext = m_context->renderPassFactory().createDepthOnly(shadowTexture, arrayLayer);
	}

	// Create command encoder
	auto encoder = m_context->createCommandEncoder("Shadow 2D Encoder");
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

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
	ShadowPassCubeUniforms shadowCubeUniforms;
	shadowCubeUniforms.lightPos = lightPosition;
	shadowCubeUniforms.farPlane = farPlane;
	uint32_t cubeMapSize = shadowTexture->getWidth();
	auto encoder = m_context->createCommandEncoder(("Shadow Cube Encoder " + std::to_string(cubeIndex)).c_str());

	// Perspective projection (90° FOV)
	glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);

	for (const auto &face : CUBE_FACES)
	{
		glm::mat4 view = glm::lookAt(
			lightPosition,
			lightPosition + face.target,
			face.up
		);
		glm::mat4 lightVP = projection * view;

		// Update uniforms
		shadowCubeUniforms.lightViewProjectionMatrix = lightVP;
		m_shadowPassCubeBindGroup[face.faceIndex]->updateBuffer(
			0,
			&shadowCubeUniforms,
			sizeof(ShadowPassCubeUniforms),
			0,
			m_context->getQueue()
		);

		uint32_t layerIndex = cubeIndex * 6 + face.faceIndex;

		std::shared_ptr<webgpu::WebGPURenderPassContext> renderPassContext = nullptr;
		if (m_isDebugMode)
		{
			// Debug: Clear each face to a solid color for visualization
			renderPassContext = m_context->renderPassFactory().create(
				DEBUG_SHADOW_CUBE_ARRAY,
				shadowTexture,
				ClearFlags::SolidColor | ClearFlags::Depth,
				glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), // Clear to black
				layerIndex,
				layerIndex
			);
		}
		else
		{
			// Normal: Clear depth only
			renderPassContext = m_context->renderPassFactory().createDepthOnly(shadowTexture, layerIndex);
		}

		wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

		renderPass.setViewport(0.0f, 0.0f, static_cast<float>(cubeMapSize), static_cast<float>(cubeMapSize), 0.0f, 1.0f);
		renderPass.setScissorRect(0, 0, cubeMapSize, cubeMapSize);

		renderPass.setBindGroup(0, m_shadowPassCubeBindGroup[face.faceIndex]->getBindGroup(), 0, nullptr);

		// Render all items
		renderItems(renderPass, items, indicesToRender, true);

		renderPassContext->end(renderPass);
	}

	m_context->submitCommandEncoder(encoder, ("Shadow Cube Commands " + std::to_string(cubeIndex)).c_str());
}

std::shared_ptr<webgpu::WebGPUPipeline> ShadowPass::getOrCreatePipeline(
	engine::rendering::Topology::Type topology,
	bool isCubeShadow
)
{
	// Use topology type as cache key (NOT mesh pointer)
	int cacheKey = static_cast<int>(topology) + (m_isDebugMode ? 1000 : 0);

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
	auto shaderName = isCubeShadow ? shader::defaults ::SHADOW_PASS_CUBE : shader::defaults::SHADOW_PASS_2D;
	auto shadowShader = m_context->shaderRegistry().getShader(shaderName);
	if (!shadowShader || !shadowShader->isValid())
	{
		spdlog::error("Shadow shader '{}' not found or invalid", shaderName);
		return nullptr;
	}

	// Create pipeline for depth-only rendering
	auto pipeline = m_context->pipelineManager().getOrCreatePipeline(
		shadowShader, // Shader
		// wgpu::TextureFormat::Undefined,	   // Color format
		m_isDebugMode ? wgpu::TextureFormat::RGBA8Unorm : wgpu::TextureFormat::Undefined,
		wgpu::TextureFormat::Depth32Float, // Depth format
		topology,						   // Topology from mesh
		wgpu::CullMode::None,			   // Cull mode
		1								   // Sample count
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
	if(indicesToRender.empty())
		return;
	std::shared_ptr<webgpu::WebGPUPipeline> currentPipeline = nullptr;
	const webgpu::WebGPUMesh *currentMesh = nullptr;

	int renderedCount = 0;

	for (const auto &index : indicesToRender)
	{
		if (index >= gpuItems.size())
		{
			spdlog::warn("Index {} out of bounds (gpuItems.size = {})", index, gpuItems.size());
			continue;
		}

		const auto &optionalItem = gpuItems[index];
		if (!optionalItem.has_value())
			continue;

		const auto &item = optionalItem.value();
		if (!item.gpuMesh || !item.gpuMaterial || !item.objectBindGroup)
			continue;

		if (item.gpuMesh != currentMesh)
		{
			auto cpuMesh = item.gpuMesh->getCPUHandle().get();
			if (!cpuMesh.has_value())
				continue;

			currentPipeline = getOrCreatePipeline(cpuMesh.value()->getTopology(), isCubeShadow);
			if (!currentPipeline || !currentPipeline->isValid())
				continue;
			renderPass.setPipeline(currentPipeline->getPipeline());
		}

		if (!RenderPass::bind(renderPass, currentPipeline->getShaderInfo(), item.objectBindGroup))
		{
			spdlog::warn("Failed to bind object bind group for shadow pass");
			continue;
		}
		// Bind vertex/index buffers only when mesh changes
		if (item.gpuMesh != currentMesh)
		{
			currentMesh = item.gpuMesh;
			currentMesh->bindBuffers(renderPass, currentPipeline->getVertexLayout());
		}

		// Draw submesh
		item.gpuMesh->isIndexed()
			? renderPass.drawIndexed(item.submesh.indexCount, 1, item.submesh.indexOffset, 0, 0)
			: renderPass.draw(item.submesh.indexCount, 1, item.submesh.indexOffset, 0);

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
