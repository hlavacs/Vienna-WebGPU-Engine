#include "engine/rendering/ShadowPass.h"

#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include "engine/math/Frustum.h"
#include "engine/rendering/BindGroupBinder.h"
#include "engine/rendering/FrameCache.h"
#include "engine/rendering/Light.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderItemGPU.h"
#include "engine/rendering/RenderingConstants.h"
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
	glm::vec3 target, up;
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

ShadowPass::ShadowPass(std::shared_ptr<webgpu::WebGPUContext> context) : RenderPass(context) {}

bool ShadowPass::initialize()
{
	spdlog::info("Initializing ShadowPass");

	auto shadowShader = m_context->shaderRegistry().getShader(shader::defaults::SHADOW_PASS_2D);
	auto shadowCubeShader = m_context->shaderRegistry().getShader(shader::defaults::SHADOW_PASS_CUBE);

	if (!shadowShader || !shadowShader->isValid() || !shadowCubeShader || !shadowCubeShader->isValid())
	{
		spdlog::error("Shadow shaders not found or invalid");
		return false;
	}

	m_shadowPass2DBindGroupLayout = shadowShader->getBindGroupLayout(bindgroup::defaults::SHADOW_PASS_2D);
	m_shadowPassCubeBindGroupLayout = shadowCubeShader->getBindGroupLayout(bindgroup::defaults::SHADOW_PASS_CUBE);

	if (!m_shadowPass2DBindGroupLayout || !m_shadowPassCubeBindGroupLayout)
	{
		spdlog::error("Failed to get bind group layouts");
		return false;
	}

	m_shadowPass2DBindGroup = m_context->bindGroupFactory().createBindGroup(
		m_shadowPass2DBindGroupLayout,
		{},
		nullptr,
		"Shadow Pass 2D"
	);

	for (int i = 0; i < 6; ++i)
	{
		m_shadowPassCubeBindGroup[i] = m_context->bindGroupFactory().createBindGroup(
			m_shadowPassCubeBindGroupLayout,
			{},
			nullptr,
			"Shadow Pass Cube"
		);
	}

	auto shadowLayout = m_context->bindGroupFactory().getGlobalBindGroupLayout(bindgroup::defaults::SHADOW);
	if (!shadowLayout)
	{
		spdlog::error("Failed to get shadow bind group layout");
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

	m_shadowBindGroup = m_context->bindGroupFactory().createBindGroup(
		shadowLayout,
		{{{4, 0}, webgpu::BindGroupResource(m_shadowSampler)},
		 {{4, 1}, webgpu::BindGroupResource(m_shadow2DArray)},
		 {{4, 2}, webgpu::BindGroupResource(m_shadowCubeArray)}},
		nullptr,
		"Shadow Maps"
	);

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

	spdlog::info("ShadowPass initialized");
	return true;
}

std::vector<ShadowUniform> ShadowPass::computeShadowUniforms(
	const ShadowRequest &request,
	const RenderTarget &renderTarget,
	float splitLambda
)
{
	std::vector<ShadowUniform> result;
	const Light *light = request.light;

	std::visit([&](auto &&lightData)
			   {
		using T = std::decay_t<decltype(lightData)>;

		// Only process shadow-casting lights
		if constexpr (!std::is_same_v<T, AmbientLight>)
		{
			ShadowUniform u{};
			u.bias = lightData.shadowBias;
			u.normalBias = lightData.shadowNormalBias;
			u.texelSize = 1.0f / static_cast<float>(lightData.shadowMapSize);
			u.pcfKernel = lightData.shadowPCFKernel;
			u.textureIndex = request.textureIndexStart;

			if constexpr (std::is_same_v<T, DirectionalLight>)
			{
				glm::vec3 dir = glm::normalize(light->getTransform()[2]);
				glm::mat4 lightView = glm::lookAt(-dir * lightData.range, glm::vec3(0), glm::vec3(0, 1, 0));

				if (request.cascadeCount > 1)
				{
					auto cascades = engine::math::Frustum::computeCascades(
						renderTarget.frustum, renderTarget.viewMatrix, lightView,
						renderTarget.nearPlane, renderTarget.farPlane, lightData.range,
						request.cascadeCount, splitLambda
					);

					for (uint32_t i = 0; i < request.cascadeCount; ++i)
					{
						ShadowUniform cascade = u;
						cascade.viewProj = cascades[i].viewProj;
						cascade.near = cascades[i].near;
						cascade.far = cascades[i].far;
						cascade.cascadeSplit = cascades[i].cascadeSplit;
						cascade.shadowType = 0;
						cascade.textureIndex = request.textureIndexStart + i;
						result.push_back(cascade);
					}
					return;
				}

				float r = lightData.range;
				u.viewProj = glm::ortho(-r, r, -r, r, -lightData.range, lightData.range * 2.0f) * lightView;
				u.near = renderTarget.nearPlane;
				u.far = renderTarget.farPlane;
				u.cascadeSplit = renderTarget.farPlane;
				u.shadowType = 0;
			}
			else if constexpr (std::is_same_v<T, SpotLight>)
			{
				glm::vec3 pos = light->getTransform()[3];
				glm::vec3 dir = glm::normalize(light->getTransform()[2]);
				u.viewProj = glm::perspective(lightData.spotAngle * 2.0f, 1.0f, 0.1f, lightData.range) *
							 glm::lookAt(pos, pos + dir, glm::vec3(0, 1, 0));
				u.near = 0.1f;
				u.far = lightData.range;
				u.cascadeSplit = lightData.range;
				u.shadowType = 0;
			}
			else if constexpr (std::is_same_v<T, PointLight>)
			{
				u.lightPos = light->getTransform()[3];
				u.near = 0.1f;
				u.far = lightData.range * 1.05f + 0.1f;
				u.cascadeSplit = lightData.range;
				u.shadowType = 1;
			}

			result.push_back(u);
		} },
			   light->getData());

	return result;
}

void ShadowPass::render(FrameCache &frameCache)
{
	if (!m_collector || frameCache.shadowRequests.empty())
		return;

	const auto &renderTarget = frameCache.renderTargets[m_cameraId];
	frameCache.shadowUniforms.clear();

	// Compute all shadow uniforms
	size_t totalUniforms = 0;
	for (const auto &req : frameCache.shadowRequests)
		totalUniforms += req.cascadeCount;
	frameCache.shadowUniforms.reserve(totalUniforms);

	for (const auto &req : frameCache.shadowRequests)
	{
		float lambda = (req.type == ShadowType::Directional) ? req.light->asDirectional().splitLambda : 0.5f;
		auto uniforms = computeShadowUniforms(req, renderTarget, lambda);
		frameCache.shadowUniforms.insert(frameCache.shadowUniforms.end(), uniforms.begin(), uniforms.end());
	}

	// Render shadow maps
	size_t idx = 0;
	for (const auto &req : frameCache.shadowRequests)
	{
		if (req.type == ShadowType::PointCube)
		{
			const auto &u = frameCache.shadowUniforms[idx++];
			auto vis = m_collector->extractForPointLight(u.lightPos, req.light->asPoint().range);
			frameCache.prepareGPUResources(m_context, *m_collector, vis);
			renderShadowCube(frameCache, vis, req.textureIndexStart, u);
		}
		else if (req.type == ShadowType::Directional && req.cascadeCount > 1)
		{
			for (uint32_t i = 0; i < req.cascadeCount; ++i)
			{
				const auto &u = frameCache.shadowUniforms[idx++];
				auto vis = m_collector->extractForLightFrustum(engine::math::Frustum::fromViewProjection(u.viewProj));
				frameCache.prepareGPUResources(m_context, *m_collector, vis);
				renderShadow2D(frameCache, vis, req.textureIndexStart + i, u);
			}
		}
		else
		{
			const auto &u = frameCache.shadowUniforms[idx++];
			auto vis = m_collector->extractForLightFrustum(engine::math::Frustum::fromViewProjection(u.viewProj));
			frameCache.prepareGPUResources(m_context, *m_collector, vis);
			renderShadow2D(frameCache, vis, req.textureIndexStart, u);
		}
	}

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
	FrameCache &frameCache,
	const std::vector<size_t> &indicesToRender,
	uint32_t arrayLayer,
	const ShadowUniform &shadowUniform
)
{
	ShadowPass2DUniforms uniforms{shadowUniform.viewProj, shadowUniform.lightPos, shadowUniform.far};
	m_shadowPass2DBindGroup->updateBuffer(0, &uniforms, sizeof(uniforms), 0, m_context->getQueue());

	uint32_t size = m_shadow2DArray->getWidth();
	auto ctx = m_isDebugMode
				   ? m_context->renderPassFactory().create(DEBUG_SHADOW_2D_ARRAY, m_shadow2DArray, ClearFlags::SolidColor | ClearFlags::Depth, glm::vec4(0), arrayLayer, arrayLayer)
				   : m_context->renderPassFactory().createDepthOnly(m_shadow2DArray, arrayLayer);

	auto encoder = m_context->createCommandEncoder("Shadow 2D");
	wgpu::RenderPassEncoder pass = encoder.beginRenderPass(ctx->getRenderPassDescriptor());
	pass.setViewport(0, 0, size, size, 0, 1);
	pass.setScissorRect(0, 0, size, size);

	renderItems(pass, frameCache, indicesToRender, false);

	ctx->end(pass);
	m_context->submitCommandEncoder(encoder, "Shadow 2D");
}

void ShadowPass::renderShadowCube(
	FrameCache &frameCache,
	const std::vector<size_t> &indicesToRender,
	uint32_t cubeIndex,
	const ShadowUniform &shadowUniform
)
{
	uint32_t size = m_shadowCubeArray->getWidth();
	glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, shadowUniform.far);
	auto encoder = m_context->createCommandEncoder("Shadow Cube");

	for (const auto &face : CUBE_FACES)
	{
		ShadowPassCubeUniforms uniforms{
			proj * glm::lookAt(shadowUniform.lightPos, shadowUniform.lightPos + face.target, face.up),
			shadowUniform.lightPos,
			shadowUniform.far
		};

		m_shadowPassCubeBindGroup[face.faceIndex]->updateBuffer(0, &uniforms, sizeof(uniforms), 0, m_context->getQueue());

		uint32_t layer = cubeIndex * 6 + face.faceIndex;
		auto ctx = m_isDebugMode
					   ? m_context->renderPassFactory().create(DEBUG_SHADOW_CUBE_ARRAY, m_shadowCubeArray, ClearFlags::SolidColor | ClearFlags::Depth, glm::vec4(0), layer, layer)
					   : m_context->renderPassFactory().createDepthOnly(m_shadowCubeArray, layer);

		wgpu::RenderPassEncoder pass = encoder.beginRenderPass(ctx->getRenderPassDescriptor());
		pass.setViewport(0, 0, size, size, 0, 1);
		pass.setScissorRect(0, 0, size, size);

		renderItems(pass, frameCache, indicesToRender, true, face.faceIndex);

		ctx->end(pass);
	}

	m_context->submitCommandEncoder(encoder, "Shadow Cube");
}

std::shared_ptr<webgpu::WebGPUPipeline> ShadowPass::getOrCreatePipeline(Topology::Type topology, bool isCube)
{
	int key = static_cast<int>(topology) + (m_isDebugMode ? 1000 : 0);
	auto &cache = isCube ? m_cubePipelineCache : m_pipelineCache;

	auto it = cache.find(key);
	if (it != cache.end())
	{
		if (auto p = it->second.lock(); p && p->isValid())
			return p;
		cache.erase(it);
	}

	auto shader = m_context->shaderRegistry().getShader(
		isCube ? shader::defaults::SHADOW_PASS_CUBE : shader::defaults::SHADOW_PASS_2D
	);

	if (!shader || !shader->isValid())
		return nullptr;

	auto pipeline = m_context->pipelineManager().getOrCreatePipeline(
		shader,
		m_isDebugMode ? wgpu::TextureFormat::RGBA8Unorm : wgpu::TextureFormat::Undefined,
		wgpu::TextureFormat::Depth32Float,
		topology,
		wgpu::CullMode::None,
		1
	);

	if (pipeline && pipeline->isValid())
		cache[key] = pipeline;

	return pipeline;
}

void ShadowPass::renderItems(
	wgpu::RenderPassEncoder &pass,
	FrameCache &frameCache,
	const std::vector<size_t> &indices,
	bool isCube,
	uint32_t faceIdx
)
{
	if (indices.empty())
		return;

	BindGroupBinder binder(&frameCache);
	std::shared_ptr<webgpu::WebGPUPipeline> pipeline;
	const webgpu::WebGPUMesh *mesh = nullptr;

	auto shadowBG = isCube ? m_shadowPassCubeBindGroup[faceIdx] : m_shadowPass2DBindGroup;
	auto shadowType = isCube ? BindGroupType::ShadowPassCube : BindGroupType::ShadowPass2D;

	for (size_t idx : indices)
	{
		if (idx >= frameCache.gpuRenderItems.size() || !frameCache.gpuRenderItems[idx].has_value())
			continue;

		const auto &item = frameCache.gpuRenderItems[idx].value();
		if (!item.gpuMesh || !item.objectBindGroup)
			continue;

		auto cpuMesh = item.gpuMesh->getCPUHandle().get();
		if (!cpuMesh.has_value())
			continue;

		if (item.gpuMesh != mesh)
		{
			pipeline = getOrCreatePipeline(cpuMesh.value()->getTopology(), isCube);
			if (!pipeline || !pipeline->isValid())
				continue;

			pass.setPipeline(pipeline->getPipeline());
			mesh = item.gpuMesh;
			mesh->bindBuffers(pass, pipeline->getVertexLayout());
		}

		binder.bind(pass, pipeline, 0, {{BindGroupType::Object, item.objectBindGroup}, {shadowType, shadowBG}});

		item.gpuMesh->isIndexed()
			? pass.drawIndexed(item.submesh.indexCount, 1, item.submesh.indexOffset, 0, 0)
			: pass.draw(item.submesh.indexCount, 1, item.submesh.indexOffset, 0);
	}
}

void ShadowPass::cleanup()
{
	m_pipelineCache.clear();
	m_cubePipelineCache.clear();
}

} // namespace engine::rendering