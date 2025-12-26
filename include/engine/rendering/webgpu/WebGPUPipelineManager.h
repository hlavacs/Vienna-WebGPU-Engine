#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/Mesh.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include "engine/resources/ResourceManagerBase.h"

namespace engine::rendering::webgpu
{

class WebGPUContext;

/**
 * @brief Configuration for creating a WebGPU pipeline.
 * Contains all parameters needed to define a pipeline.
 */
struct PipelineKey
{
	std::shared_ptr<WebGPUShaderInfo> shaderInfo; // enthält VertexLayout, ShaderFeatures, enableDepth/Blend
	wgpu::TextureFormat colorFormat;			  // vom RenderTarget
	wgpu::TextureFormat depthFormat;			  // vom RenderTarget
	engine::rendering::Topology::Type topology;	  // vom Mesh
	wgpu::CullMode cullMode;					  // von Material / Default
	uint32_t sampleCount;						  // MSAA, vom RenderTarget / global

	bool operator==(const PipelineKey &other) const
	{
		return shaderInfo == other.shaderInfo && colorFormat == other.colorFormat && depthFormat == other.depthFormat && topology == other.topology && cullMode == other.cullMode && sampleCount == other.sampleCount;
	}
};

/**
 * @brief Hash function for PipelineKey to be used in unordered_map.
 */
struct PipelineKeyHasher
{
	std::size_t operator()(const PipelineKey &key) const
	{
		std::size_t h = std::hash<std::shared_ptr<WebGPUShaderInfo>>{}(key.shaderInfo);
		h ^= std::hash<int>{}(static_cast<int>(key.colorFormat)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>{}(static_cast<int>(key.depthFormat)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>{}(static_cast<int>(key.topology)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>{}(static_cast<int>(key.cullMode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint32_t>{}(key.sampleCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};

/**
 * @brief Manages render pipelines with hot-reloading support.
 *
 * Caches pipelines based on shader, vertex layout, features, and render target formats.
 */
class WebGPUPipelineManager
{
  public:
	WebGPUPipelineManager(WebGPUContext &context);
	~WebGPUPipelineManager();

	/**
	 * @brief Get or create a pipeline for a mesh, material, and render target.
	 *
	 * The key is generated internally using shader info, features, vertex layout, and render target formats.
	 */
	std::shared_ptr<WebGPUPipeline> getOrCreatePipeline(
		const std::shared_ptr<engine::rendering::Mesh> &mesh,
		const std::shared_ptr<engine::rendering::Material> &material,
		const std::shared_ptr<engine::rendering::webgpu::WebGPURenderPassContext> &renderPass
	);

	/**
	 * @brief Reloads a pipeline from its shader file.
	 * @param pipeline Pipeline to reload.
	 * @return True if reload was successful.
	 */
	bool reloadPipeline(std::shared_ptr<WebGPUPipeline> pipeline);

	/**
	 * @brief Reloads all registered pipelines.
	 * @return Number of successfully reloaded pipelines.
	 */
	size_t reloadAllPipelines();

	/**
	 * @brief Clears all cached pipelines.
	 */
	void cleanup();

  private:
	WebGPUContext &m_context;

	std::unordered_map<PipelineKey, std::shared_ptr<WebGPUPipeline>, PipelineKeyHasher> m_pipelines;

	bool createPipelineInternal(const PipelineKey &key, std::shared_ptr<WebGPUPipeline> &outPipeline);
};

} // namespace engine::rendering::webgpu