#include "engine/rendering/ShadowPass.h"

#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include "engine/rendering/Mesh.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

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
	m_context(context)
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
	m_shadowBindGroupLayout = shadowShader->getBindGroupLayout(0);		   // Group 0: Shadow uniforms
	m_shadowCubeBindGroupLayout = shadowCubeShader->getBindGroupLayout(0); // Group 0: Cube shadow uniforms

	if (!m_shadowBindGroupLayout || !m_shadowCubeBindGroupLayout)
	{
		spdlog::error("Failed to get bind group layouts from shadow shaders");
		return false;
	}

	// Create uniform buffers using buffer factory
	m_shadowUniformsBuffer = m_context->bufferFactory().createUniformBufferWrapped(
		"Shadow Uniforms Buffer",
		0,
		sizeof(ShadowUniforms)
	);

	m_shadowCubeUniformsBuffer = m_context->bufferFactory().createUniformBufferWrapped(
		"Shadow Cube Uniforms Buffer",
		0,
		sizeof(ShadowCubeUniforms)
	);

	// Create reusable bind group for 2D shadows
	{
		std::vector<wgpu::BindGroupEntry> entries(1);
		entries[0].binding = 0;
		entries[0].buffer = m_shadowUniformsBuffer->getBuffer();
		entries[0].offset = 0;
		entries[0].size = sizeof(ShadowUniforms);

		wgpu::BindGroupDescriptor bgDesc{};
		bgDesc.layout = m_shadowBindGroupLayout->getLayout();
		bgDesc.label = "Shadow 2D Bind Group";
		bgDesc.entryCount = entries.size();
		bgDesc.entries = entries.data();
		wgpu::BindGroup bindGroup = m_context->getDevice().createBindGroup(bgDesc);

		m_shadowBindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
			bindGroup,
			m_shadowBindGroupLayout,
			std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{m_shadowUniformsBuffer}
		);
	}

	// Create reusable bind group for cube shadows
	{
		std::vector<wgpu::BindGroupEntry> entries(1);
		entries[0].binding = 0;
		entries[0].buffer = m_shadowCubeUniformsBuffer->getBuffer();
		entries[0].offset = 0;
		entries[0].size = sizeof(ShadowCubeUniforms);

		wgpu::BindGroupDescriptor bgDesc{};
		bgDesc.layout = m_shadowCubeBindGroupLayout->getLayout();
		bgDesc.label = "Shadow Cube Bind Group";
		bgDesc.entryCount = entries.size();
		bgDesc.entries = entries.data();
		wgpu::BindGroup bindGroup = m_context->getDevice().createBindGroup(bgDesc);

		m_shadowCubeBindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
			bindGroup,
			m_shadowCubeBindGroupLayout,
			std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{m_shadowCubeUniformsBuffer}
		);
	}

	spdlog::info("ShadowPass initialized successfully");
	return true;
}

void ShadowPass::renderShadow2D(
	const std::vector<std::optional<RenderItemGPU>> &gpuItems,
	const std::vector<size_t> &indicesToRender, // only items visible to this light
	const webgpu::WebGPUTexture &shadowTexture,
	uint32_t arrayLayer,
	const glm::mat4 &lightViewProjection
)
{
	// Prepare shadow uniforms
	ShadowUniforms shadowUniforms;
	shadowUniforms.lightViewProjectionMatrix = lightViewProjection;

	// Update uniforms using the reusable bind group
	m_shadowBindGroup->updateBuffer(0, &shadowUniforms, sizeof(ShadowUniforms), 0, m_context->getQueue());

	wgpu::TextureView layerView = shadowTexture.createLayerView(arrayLayer, "Shadow2D Layer");

	// Create render pass using factory
	auto renderPassContext = m_context->renderPassFactory().createDepthOnly(
		layerView,
		true,
		1.0f
	);

	// Create command encoder
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "Shadow 2D Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// Begin render pass
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

	// Set viewport and scissor
	uint32_t shadowMapSize = shadowTexture.getWidth();
	renderPass.setViewport(0.0f, 0.0f, static_cast<float>(shadowMapSize), static_cast<float>(shadowMapSize), 0.0f, 1.0f);
	renderPass.setScissorRect(0, 0, shadowMapSize, shadowMapSize);

	// Bind shadow uniforms (group 0)
	renderPass.setBindGroup(0, m_shadowBindGroup->getBindGroup(), 0, nullptr);

	// Render all items
	renderItems(renderPass, gpuItems, indicesToRender, false);

	// End render pass and submit
	renderPass.end();
	renderPass.release();

	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = "Shadow 2D Commands";
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
	m_context->getDevice().getQueue().submit(1, &commands);

	commands.release();
	encoder.release();
	layerView.release();
}

void ShadowPass::renderShadowCube(
	const std::vector<std::optional<RenderItemGPU>> &items,
	const std::vector<size_t> &indicesToRender,
	const webgpu::WebGPUTexture &shadowTexture,
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
		ShadowCubeUniforms shadowCubeUniforms;
		shadowCubeUniforms.lightPosition = lightPosition;
		shadowCubeUniforms.farPlane = farPlane;
		m_shadowCubeBindGroup->updateBuffer(
			0,
			&shadowCubeUniforms,
			sizeof(ShadowCubeUniforms),
			0,
			m_context->getQueue()
		);

		uint32_t layerIndex = cubeIndex * 6 + face.faceIndex;
		std::string faceLabel = "Shadow Cube Face " + std::to_string(face.faceIndex);
		wgpu::TextureView depthView = shadowTexture.createLayerView(layerIndex, faceLabel.c_str());

		auto renderPassContext = m_context->renderPassFactory().createDepthOnly(
			depthView,
			true, // clearDepth
			1.0f
		);

		wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

		uint32_t cubeMapSize = shadowTexture.getWidth();
		renderPass.setViewport(0.0f, 0.0f, static_cast<float>(cubeMapSize), static_cast<float>(cubeMapSize), 0.0f, 1.0f);
		renderPass.setScissorRect(0, 0, cubeMapSize, cubeMapSize);

		renderPass.setBindGroup(0, m_shadowCubeBindGroup->getBindGroup(), 0, nullptr);

		// Render all items
		renderItems(renderPass, items, indicesToRender, true);

		renderPass.end();
		renderPass.release();
		depthView.release();
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

	// Check cache
	auto it = cache.find(cacheKey);
	if (it != cache.end())
	{
		return it->second;
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
	auto pipeline = m_context->pipelineFactory().createRenderPipeline(
		shadowShader,					  // Vertex shader
		shadowShader,					  // Fragment shader (depth-only)
		wgpu::TextureFormat::Undefined,	  // No color attachment
		wgpu::TextureFormat::Depth24Plus, // Depth format
		topology,						  // Topology from mesh
		wgpu::CullMode::Back,			  // Cull back faces for shadows
		1								  // Sample count
	);

	if (!pipeline || !pipeline->isValid())
	{
		spdlog::error("Failed to create shadow pipeline");
		return nullptr;
	}

	// Cache pipeline (by topology type, not per-mesh instance)
	cache[cacheKey] = pipeline;

	return pipeline;
}

void ShadowPass::clearPipelineCache()
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

		// Bind vertex/index buffers if mesh changed
		if (gpuItem.gpuMesh != currentMesh)
		{
			currentMesh = gpuItem.gpuMesh;
			gpuItem.gpuMesh->bindBuffers(renderPass, currentPipeline->getVertexShaderInfo()->getVertexLayout());
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
	}
}

} // namespace engine::rendering
