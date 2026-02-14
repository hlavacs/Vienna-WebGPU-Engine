#include "engine/rendering/ShadowPass.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <spdlog/spdlog.h>

#include "engine/math/Frustum.h"
#include "engine/rendering/BindGroupBinder.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/ShadowRequest.h"
#include "engine/rendering/ShadowUniforms.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering
{

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

	auto shadowShader = m_context->shaderRegistry().getShader(shader::defaults::SHADOW_PASS_2D);
	if (!shadowShader || !shadowShader->isValid())
	{
		spdlog::error("Shadow shader not found in registry");
		return false;
	}

	auto shadowCubeShader = m_context->shaderRegistry().getShader(shader::defaults::SHADOW_PASS_CUBE);
	if (!shadowCubeShader || !shadowCubeShader->isValid())
	{
		spdlog::error("Shadow cube shader not found in registry");
		return false;
	}

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

	// Initialize shadow map resources
	{
		auto shadowLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout(bindgroup::defaults::SHADOW);
		if (!shadowLayout)
		{
			spdlog::error("Failed to get shadowMaps bind group layout");
			return false;
		}

		m_shadowSampler = m_context->samplerFactory().getShadowComparisonSampler();
		m_shadow2DArray = m_context->textureFactory().createShadowMap2DArray(
			constants::DEFAULT_SHADOW_MAP_SIZE,
			constants::MAX_SHADOW_MAPS_2D
		);
		m_shadowCubeArray = m_context->textureFactory().createShadowMapCubeArray(
			constants::DEFAULT_CUBE_SHADOW_MAP_SIZE,
			constants::MAX_SHADOW_MAPS_CUBE
		);

		std::map<webgpu::BindGroupBindingKey, webgpu::BindGroupResource> resources = {
			{{4, 0}, webgpu::BindGroupResource(m_shadowSampler)},
			{{4, 1}, webgpu::BindGroupResource(m_shadow2DArray)},
			{{4, 2}, webgpu::BindGroupResource(m_shadowCubeArray)}
		};

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

	std::visit([&](auto &&specificLight)
			   {
		using T = std::decay_t<decltype(specificLight)>;

		if constexpr (std::is_same_v<T, DirectionalLight>) {
			glm::vec3 dir = glm::normalize(light->getTransform()[2]);
			glm::mat4 lightView = glm::lookAt(
				-dir * specificLight.range, glm::vec3(0), glm::vec3(0, 1, 0));

			if (request.cascadeCount > 1) {
				auto cascades = engine::math::Frustum::computeCascades(
					renderTarget.frustum, renderTarget.viewMatrix, lightView,
					renderTarget.nearPlane, renderTarget.farPlane, specificLight.range,
					request.cascadeCount, splitLambda);

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
				u.near = renderTarget.nearPlane;
				u.far = renderTarget.farPlane;
				u.cascadeSplit = renderTarget.farPlane;
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
			u.far = specificLight.range * 1.05f + 0.1f; // Slightly extend far plane to avoid clipping
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

	// Count and reserve shadow uniforms
	size_t totalUniforms = 0;
	for (const auto &request : frameCache.shadowRequests)
		totalUniforms += request.cascadeCount;
	frameCache.shadowUniforms.reserve(totalUniforms);

	// Create shadow uniforms
	for (const auto &request : frameCache.shadowRequests)
	{
		float splitLambda = (request.type == ShadowType::Directional)
								? request.light->asDirectional().splitLambda
								: 0.5f;

		auto uniforms = computeShadowUniforms(request, renderTarget, splitLambda);
		frameCache.shadowUniforms.insert(frameCache.shadowUniforms.end(), uniforms.begin(), uniforms.end());
	}

	// Render shadow maps
	size_t uniformIndex = 0;
	for (const auto &request : frameCache.shadowRequests)
	{
		if (request.type == ShadowType::PointCube)
		{
			const auto &u = frameCache.shadowUniforms[uniformIndex++];
			auto visibleIndices = m_collector->extractForPointLight(u.lightPos, request.light->asPoint().range);
			frameCache.prepareGPUResources(m_context, *m_collector, visibleIndices);
			renderShadowCube(frameCache, frameCache.gpuRenderItems, visibleIndices, m_shadowCubeArray, request.textureIndexStart, u.lightPos, request.light->asPoint().range);
		}
		else if (request.type == ShadowType::Directional && request.cascadeCount > 1)
		{
			for (uint32_t i = 0; i < request.cascadeCount; ++i)
			{
				const auto &u = frameCache.shadowUniforms[uniformIndex++];
				auto visibleIndices = m_collector->extractForLightFrustum(
					engine::math::Frustum::fromViewProjection(u.viewProj)
				);
				frameCache.prepareGPUResources(m_context, *m_collector, visibleIndices);
				renderShadow2D(frameCache, frameCache.gpuRenderItems, visibleIndices, m_shadow2DArray, request.textureIndexStart + i, u.viewProj, u.lightPos, u.far);
			}
		}
		else
		{
			const auto &u = frameCache.shadowUniforms[uniformIndex++];
			auto visibleIndices = m_collector->extractForLightFrustum(
				engine::math::Frustum::fromViewProjection(u.viewProj)
			);
			frameCache.prepareGPUResources(m_context, *m_collector, visibleIndices);
			renderShadow2D(frameCache, frameCache.gpuRenderItems, visibleIndices, m_shadow2DArray, request.textureIndexStart, u.viewProj, u.lightPos, u.far);
		}
	}

	// Update GPU shadow uniform buffer
	if (!frameCache.shadowUniforms.empty())
	{
		m_shadowBindGroup->updateBuffer(3, frameCache.shadowUniforms.data(), frameCache.shadowUniforms.size() * sizeof(ShadowUniform), 0, m_context->getQueue());
	}
}

void ShadowPass::renderShadow2D(
	FrameCache &frameCache,
	const std::vector<std::optional<RenderItemGPU>> &gpuItems,
	const std::vector<size_t> &indicesToRender,
	const std::shared_ptr<webgpu::WebGPUTexture> shadowTexture,
	uint32_t arrayLayer,
	const glm::mat4 &lightViewProjection,
	const glm::vec3 &lightPosition,
	float farPlane
)
{
	ShadowPass2DUniforms shadowUniforms;
	shadowUniforms.lightViewProjectionMatrix = lightViewProjection;
	shadowUniforms.lightPos = lightPosition;
	shadowUniforms.farPlane = farPlane;

	uint32_t shadowMapSize = shadowTexture->getWidth();
	m_shadowPass2DBindGroup->updateBuffer(0, &shadowUniforms, sizeof(ShadowPass2DUniforms), 0, m_context->getQueue());

	auto renderPassContext = m_isDebugMode
								 ? m_context->renderPassFactory().create(DEBUG_SHADOW_2D_ARRAY, shadowTexture, ClearFlags::SolidColor | ClearFlags::Depth, glm::vec4(0.0f), arrayLayer, arrayLayer)
								 : m_context->renderPassFactory().createDepthOnly(shadowTexture, arrayLayer);

	auto encoder = m_context->createCommandEncoder("Shadow 2D Encoder");
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

	renderPass.setViewport(0.0f, 0.0f, static_cast<float>(shadowMapSize), static_cast<float>(shadowMapSize), 0.0f, 1.0f);
	renderPass.setScissorRect(0, 0, shadowMapSize, shadowMapSize);


	renderItems(renderPass, frameCache, gpuItems, indicesToRender, false);
	renderPassContext->end(renderPass);
	m_context->submitCommandEncoder(encoder, "Shadow 2D Commands");
}

void ShadowPass::renderShadowCube(
	FrameCache &frameCache,
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
	glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);

	for (const auto &face : CUBE_FACES)
	{
		glm::mat4 view = glm::lookAt(lightPosition, lightPosition + face.target, face.up);
		shadowCubeUniforms.lightViewProjectionMatrix = projection * view;
		m_shadowPassCubeBindGroup[face.faceIndex]->updateBuffer(0, &shadowCubeUniforms, sizeof(ShadowPassCubeUniforms), 0, m_context->getQueue());

		uint32_t layerIndex = cubeIndex * 6 + face.faceIndex;
		auto renderPassContext = m_isDebugMode
									 ? m_context->renderPassFactory().create(DEBUG_SHADOW_CUBE_ARRAY, shadowTexture, ClearFlags::SolidColor | ClearFlags::Depth, glm::vec4(0.0f), layerIndex, layerIndex)
									 : m_context->renderPassFactory().createDepthOnly(shadowTexture, layerIndex);

		wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());
		renderPass.setViewport(0.0f, 0.0f, static_cast<float>(cubeMapSize), static_cast<float>(cubeMapSize), 0.0f, 1.0f);
		renderPass.setScissorRect(0, 0, cubeMapSize, cubeMapSize);

		renderItems(renderPass, frameCache, items, indicesToRender, true, face.faceIndex);
		renderPassContext->end(renderPass);
	}

	m_context->submitCommandEncoder(encoder, ("Shadow Cube Commands " + std::to_string(cubeIndex)).c_str());
}

std::shared_ptr<webgpu::WebGPUPipeline> ShadowPass::getOrCreatePipeline(
	engine::rendering::Topology::Type topology, bool isCubeShadow
)
{
	int cacheKey = static_cast<int>(topology) + (m_isDebugMode ? 1000 : 0);
	auto &cache = isCubeShadow ? m_cubePipelineCache : m_pipelineCache;

	auto it = cache.find(cacheKey);
	if (it != cache.end())
	{
		auto pipeline = it->second.lock();
		if (pipeline && pipeline->isValid())
			return pipeline;
		cache.erase(it);
	}

	auto shaderName = isCubeShadow ? shader::defaults::SHADOW_PASS_CUBE : shader::defaults::SHADOW_PASS_2D;
	auto shadowShader = m_context->shaderRegistry().getShader(shaderName);
	if (!shadowShader || !shadowShader->isValid())
	{
		spdlog::error("Shadow shader '{}' not found or invalid", shaderName);
		return nullptr;
	}

	auto pipeline = m_context->pipelineManager().getOrCreatePipeline(
		shadowShader,
		m_isDebugMode ? wgpu::TextureFormat::RGBA8Unorm : wgpu::TextureFormat::Undefined,
		wgpu::TextureFormat::Depth32Float,
		topology,
		wgpu::CullMode::None,
		1
	);

	if (!pipeline || !pipeline->isValid())
	{
		spdlog::error("Failed to create shadow pipeline");
		return nullptr;
	}

	cache[cacheKey] = pipeline;
	return pipeline;
}

void ShadowPass::cleanup()
{
	m_pipelineCache.clear();
	m_cubePipelineCache.clear();
}

void ShadowPass::renderItems(
	wgpu::RenderPassEncoder &renderPass,
	FrameCache &frameCache,
	const std::vector<std::optional<RenderItemGPU>> &gpuItems,
	const std::vector<size_t> &indicesToRender,
	bool isCubeShadow,
	uint32_t faceIndex
)
{
	if (indicesToRender.empty())
		return;

	BindGroupBinder binder(&frameCache);
	std::shared_ptr<webgpu::WebGPUPipeline> currentPipeline = nullptr;
	const webgpu::WebGPUMesh *currentMesh = nullptr;
	int renderedCount = 0;

	// Get shadow pass bind group once
	auto shadowPassBindGroup = isCubeShadow 
		? m_shadowPassCubeBindGroup[faceIndex] 
		: m_shadowPass2DBindGroup;

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
			binder.reset();
			
			// Bind shadow pass bind group once per pipeline
			binder.bind(
				renderPass,
				currentPipeline->getShaderInfo(),
				0,
				{
					{isCubeShadow ? webgpu::BindGroupType::ShadowPassCube : webgpu::BindGroupType::ShadowPass2D, shadowPassBindGroup}
				}
			);
		}

		// Bind per-object bind group
		binder.bindGroup(renderPass, currentPipeline->getShaderInfo(), item.objectBindGroup);

		if (item.gpuMesh != currentMesh)
		{
			currentMesh = item.gpuMesh;
			currentMesh->bindBuffers(renderPass, currentPipeline->getVertexLayout());
		}

		item.gpuMesh->isIndexed()
			? renderPass.drawIndexed(item.submesh.indexCount, 1, item.submesh.indexOffset, 0, 0)
			: renderPass.draw(item.submesh.indexCount, 1, item.submesh.indexOffset, 0);

		renderedCount++;
	}

	if (renderedCount > 0)
		spdlog::debug("Shadow pass rendered {} items", renderedCount);
	else
		spdlog::warn("Shadow pass rendered 0 items!");
}

} // namespace engine::rendering