#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{

/**
 * @brief Pipeline configuration for creation and hot-reloading.
 */
struct PipelineConfig
{
	std::shared_ptr<webgpu::WebGPUShaderInfo> shaderInfo; // Shader with entry points, can be shared
	wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined;
	wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined;
	std::vector<std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo>> bindGroupLayouts;
	wgpu::PrimitiveTopology topology = wgpu::PrimitiveTopology::TriangleList;
	bool enableDepth = true;
	uint32_t vertexBufferCount = 1;
};

/**
 * @brief Manages render pipelines with hot-reloading support.
 *
 * Caches pipelines by name, handles shader reloading, and manages
 * pipeline configurations for different rendering techniques.
 */
class PipelineManager
{
  public:
	PipelineManager(webgpu::WebGPUContext &context);
	~PipelineManager();

	/**
	 * @brief Creates and registers a pipeline.
	 * @param name Pipeline identifier.
	 * @param config Pipeline configuration.
	 * @return True if successful.
	 */
	bool createPipeline(const std::string &name, const PipelineConfig &config);

	/**
	 * @brief Gets a pipeline by name.
	 * @param name Pipeline identifier.
	 * @return The pipeline, or nullptr if not found.
	 */
	std::shared_ptr<webgpu::WebGPUPipeline> getPipeline(const std::string &name) const;

	/**
	 * @brief Reloads a pipeline from its shader file.
	 * @param name Pipeline identifier.
	 * @return True if reload was successful.
	 */
	bool reloadPipeline(const std::string &name);

	/**
	 * @brief Reloads all registered pipelines.
	 * @return Number of successfully reloaded pipelines.
	 */
	size_t reloadAllPipelines();

	/**
	 * @brief Removes a pipeline.
	 * @param name Pipeline identifier.
	 */
	void removePipeline(const std::string &name);

	/**
	 * @brief Clears all pipelines.
	 */
	void clear();

  private:
	struct PipelineEntry
	{
		std::shared_ptr<webgpu::WebGPUPipeline> pipeline;
		std::shared_ptr<webgpu::WebGPUShaderInfo> shaderInfo;
		PipelineConfig config;

		PipelineEntry() = default;
		PipelineEntry(std::shared_ptr<webgpu::WebGPUPipeline> p, 
		              std::shared_ptr<webgpu::WebGPUShaderInfo> s, 
		              const PipelineConfig &c)
			: pipeline(std::move(p)), shaderInfo(std::move(s)), config(c) {}
	};

	webgpu::WebGPUContext &m_context;
	std::unordered_map<std::string, PipelineEntry> m_pipelines;

	bool createPipelineInternal(const std::string &name, const PipelineConfig &config, PipelineEntry &entry);
};

} // namespace engine::rendering