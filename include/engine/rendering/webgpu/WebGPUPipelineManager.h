#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
class WebGPUPipelineFactory;

/**
 * @brief Configuration for creating a WebGPU pipeline.
 * Contains all parameters needed to define a pipeline.
 * Uses shader NAME as key, not shader pointer (shaders are immutable).
 */
struct PipelineKey
{
	std::string shaderName;						  // shader identifier (immutable after creation)
	wgpu::TextureFormat colorFormat;			  // vom RenderTarget
	wgpu::TextureFormat depthFormat;			  // vom RenderTarget
	engine::rendering::Topology::Type topology;	  // vom Mesh
	wgpu::CullMode cullMode;					  // von Material / Default
	uint32_t sampleCount;						  // MSAA, vom RenderTarget / global

	bool operator==(const PipelineKey &other) const
	{
		return shaderName == other.shaderName && colorFormat == other.colorFormat && depthFormat == other.depthFormat && topology == other.topology && cullMode == other.cullMode && sampleCount == other.sampleCount;
	}
};

/**
 * @brief Hash function for PipelineKey to be used in unordered_map.
 */
struct PipelineKeyHasher
{
	std::size_t operator()(const PipelineKey &key) const
	{
		std::size_t h = std::hash<std::string>{}(key.shaderName);
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
 * SINGLE ENTRY POINT for all pipeline creation and management.
 * 
 * Design Principles:
 * - All pipeline requests go through the manager (getOrCreatePipeline)
 * - Pipelines are immutable after creation
 * - Reloads use swap semantics (replace old with new, never modify in-place)
 * - Pipeline validity is guaranteed: getOrCreatePipeline always returns a valid pipeline or nullptr
 * - Reloads are deferred to frame boundaries (processPendingReloads called after frame presentation)
 * - Avoids global invalidation: only affected pipelines are reloaded
 * - Internal factory is not publicly accessible (used only by manager)
 */
class WebGPUPipelineManager
{
  public:
	WebGPUPipelineManager(WebGPUContext &context);
	~WebGPUPipelineManager();

	/**
	 * @brief Get or create a pipeline for a mesh, material, and render target.
	 *
	 * ONLY public method for obtaining pipelines.
	 * The key is generated internally using shader info, features, vertex layout, and render target formats.
	 * 
	 * @param mesh The mesh defining vertex layout and topology.
	 * @param material The material defining shader.
	 * @param renderPass The render pass defining target formats.
	 * @return Valid pipeline or nullptr on failure. Pipeline is guaranteed to remain valid until reloaded.
	 */
	std::shared_ptr<WebGPUPipeline> getOrCreatePipeline(
		const std::shared_ptr<engine::rendering::Mesh> &mesh,
		const std::shared_ptr<engine::rendering::Material> &material,
		const std::shared_ptr<engine::rendering::webgpu::WebGPURenderPassContext> &renderPass
	);

	/**
	 * @brief Get or create a pipeline with explicit parameters (no mesh/material required).
	 *
	 * Use this when you have all pipeline parameters but no mesh or material object.
	 * Useful for specialized passes like shadow rendering, compositing, etc.
	 * 
	 * @param shaderInfo The shader to use (must be valid).
	 * @param colorFormat Render target color format (or Undefined for no color).
	 * @param depthFormat Render target depth format (or Undefined for no depth).
	 * @param topology Primitive topology.
	 * @param cullMode Face culling mode.
	 * @param sampleCount MSAA sample count.
	 * @return Valid pipeline or nullptr on failure.
	 */
	std::shared_ptr<WebGPUPipeline> getOrCreatePipeline(
		const std::shared_ptr<WebGPUShaderInfo> &shaderInfo,
		wgpu::TextureFormat colorFormat,
		wgpu::TextureFormat depthFormat,
		engine::rendering::Topology::Type topology,
		wgpu::CullMode cullMode,
		uint32_t sampleCount
	);

	/**
	 * @brief Mark a pipeline for reload after current frame finishes.
	 * 
	 * Uses swap semantics: the old pipeline remains valid until processPendingReloads() is called
	 * (after frame presentation). Frames in progress will continue using the old pipeline.
	 * 
	 * @param pipeline Pipeline to reload.
	 * @return True if pipeline was marked (will be reloaded after frame).
	 */
	bool reloadPipeline(std::shared_ptr<WebGPUPipeline> pipeline);

	/**
	 * @brief Mark all pipelines for reload after current frame finishes.
	 * @return Number of pipelines marked for reload.
	 */
	size_t reloadAllPipelines();

	/**
	 * @brief Process pending pipeline reloads (call after frame finishes and is presented).
	 * 
	 * This is the only time pipelines are actually replaced in the cache.
	 * Safe to call at any time; does nothing if no reloads are pending.
	 * 
	 * @return Number of successfully reloaded pipelines.
	 */
	size_t processPendingReloads();

	/**
	 * @brief Clears all cached pipelines.
	 */
	void cleanup();

  private:
	WebGPUContext &m_context;
	std::unique_ptr<WebGPUPipelineFactory> m_pipelineFactory;

	// Pipeline cache: key -> pipeline
	// Immutable after insertion; replaced entirely on reload via processPendingReloads()
	std::unordered_map<PipelineKey, std::shared_ptr<WebGPUPipeline>, PipelineKeyHasher> m_pipelines;
	
	// Pipelines marked for reload after current frame finishes
	std::unordered_set<std::shared_ptr<WebGPUPipeline>> m_pendingReloads;

	/**
	 * @brief Internal: Create a new pipeline object (no caching, no registration).
	 * 
	 * Only called by:
	 * - getOrCreatePipeline (for new pipelines)
	 * - processPendingReloads (for reloaded pipelines)
	 * 
	 * Factory construction only; caller is responsible for cache management.
	 */
	bool createPipelineInternal(
		const PipelineKey &key,
		const std::shared_ptr<WebGPUShaderInfo> &shaderInfo,
		std::shared_ptr<WebGPUPipeline> &outPipeline
	);
};

} // namespace engine::rendering::webgpu