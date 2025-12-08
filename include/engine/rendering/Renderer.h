#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/PipelineManager.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderPassManager.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModel.h"

namespace engine::rendering
{

// Forward declarations
struct FrameUniforms;

/**
 * @brief Central renderer that orchestrates the rendering pipeline.
 *
 * Manages render passes, pipelines, and executes rendering of collected
 * scene data. Separates rendering logic from application/scene logic.
 */
class Renderer
{
  public:
	Renderer(std::shared_ptr<webgpu::WebGPUContext> context);
	~Renderer();

	/**
	 * @brief Initializes the renderer with default passes and pipelines.
	 * @return True if initialization succeeded.
	 */
	bool initialize();

	/**
	 * @brief Updates frame uniforms (camera, time, etc).
	 * @param frameUniforms The frame-level uniform data.
	 */
	void updateFrameUniforms(const FrameUniforms &frameUniforms);

	/**
	 * @brief Renders a frame using collected scene data.
	 * @param collector Collected render items and lights.
	 * @param uiCallback Optional callback for rendering UI/debug overlays.
	 */
	void renderFrame(
		const RenderCollector &collector,
		std::function<void(wgpu::RenderPassEncoder)> uiCallback = nullptr
	);

	/**
	 * @brief Handles window resize events.
	 * @param width New width.
	 * @param height New height.
	 */
	void onResize(uint32_t width, uint32_t height);

	/**
	 * @brief Gets the pipeline manager.
	 * @return Reference to pipeline manager.
	 */
	PipelineManager &pipelineManager() { return *m_pipelineManager; }

	/**
	 * @brief Gets the render pass manager.
	 * @return Reference to render pass manager.
	 */
	RenderPassManager &renderPassManager() { return *m_renderPassManager; }

  private:
	std::shared_ptr<webgpu::WebGPUContext> m_context;
	std::unique_ptr<PipelineManager> m_pipelineManager;
	std::unique_ptr<RenderPassManager> m_renderPassManager;

	// Cached resources
	std::shared_ptr<webgpu::WebGPUDepthTexture> m_depthBuffer;

	// Main shader (holds global buffers for frame, lights, object, material)
	std::shared_ptr<webgpu::WebGPUShaderInfo> m_mainShader = nullptr;

	// WebGPU model cache (CPU Model Handle -> GPU Model)
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUModel>> m_modelCache;

	// Main render pass ID
	uint64_t m_mainPassId = 0;

	bool setupDefaultPipelines();
	bool setupDefaultRenderPasses();
	void updateLights(const std::vector<LightStruct> &lights);
	
	std::shared_ptr<webgpu::WebGPUModel> getOrCreateWebGPUModel(
		const engine::core::Handle<engine::rendering::Model> &modelHandle
	);
};

} // namespace engine::rendering
