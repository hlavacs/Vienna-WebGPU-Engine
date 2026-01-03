#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/DebugCollector.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderPassManager.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine
{
class GameEngine; // forward declaration
} // namespace engine

namespace engine::rendering
{

struct RenderTarget
{
	RenderTarget() = default;
	RenderTarget(
		uint64_t cameraId,
		std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> gpuTexture,
		glm::vec4 viewport = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
		ClearFlags clearFlags = ClearFlags::SolidColor,
		glm::vec4 backgroundColor = glm::vec4(0.0f),
		std::optional<TextureHandle> cpuTarget = std::nullopt
	) :
		m_cameraId(cameraId),
		gpuTexture(std::move(gpuTexture)),
		viewport(viewport),
		clearFlags(clearFlags),
		backgroundColor(backgroundColor),
		cpuTarget(std::move(cpuTarget))
	{
	}

	std::shared_ptr<engine::rendering::webgpu::WebGPUTexture> gpuTexture; // actual GPU render target
	glm::vec4 viewport;													  // The relative viewport (x, y, width, height) in [0,1]
	ClearFlags clearFlags;
	glm::vec4 backgroundColor;
	std::optional<TextureHandle> cpuTarget; // optional CPU-side texture
	uint64_t m_cameraId;					// associated camera ID
};

/**
 * @brief Central renderer that orchestrates the rendering pipeline.
 *
 * Manages render passes, pipelines, and executes rendering of collected
 * scene data. Separates rendering logic from application/scene logic.
 */
class Renderer
{
  public:
	friend class engine::GameEngine;

	Renderer(std::shared_ptr<webgpu::WebGPUContext> context);
	~Renderer();

	/**
	 * @brief Initializes the renderer with default passes and pipelines.
	 * @return True if initialization succeeded.
	 */
	bool initialize();

  protected:
	void startFrame();

	void renderToTexture(
		const RenderCollector &collector,
		uint64_t renderTargetId, // eindeutige ID für das RenderTarget
		const glm::vec4 &viewport,
		ClearFlags clearFlags,
		const glm::vec4 &backgroundColor,
		std::optional<TextureHandle> cpuTarget,
		const FrameUniforms &frameUniforms
	);

	void compositeTexturesToSurface(
		const std::vector<uint64_t> &targets,
		std::function<void(wgpu::RenderPassEncoder)> uiCallback = nullptr
	);

	std::shared_ptr<webgpu::WebGPUTexture> updateRenderTexture(
		uint32_t renderTargetId,
		std::shared_ptr<webgpu::WebGPUTexture> &gpuTexture,
		const std::optional<Texture::Handle> &cpuTarget,
		const glm::vec4 &viewport,
		wgpu::TextureFormat format,
		wgpu::TextureUsage usageFlags
	);

	/**
	 * @brief Handles window resize events.
	 * @param width New width.
	 * @param height New height.
	 */
	void onResize(uint32_t width, uint32_t height);

  public:
	/**
	 * @brief Gets the WebGPU context.
	 * @return Pointer to WebGPU context.
	 */
	webgpu::WebGPUContext *getWebGPUContext() const { return m_context.get(); }

	/**
	 * @brief Gets the object bind group layout for creating per-instance bind groups.
	 * @return Shared pointer to object bind group layout.
	 */
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> getObjectBindGroupLayout() const
	{
		return m_objectBindGroupLayout;
	}

  protected:
	/**
	 * @brief Gets the pipeline manager.
	 * @return Reference to pipeline manager.
	 */
	webgpu::WebGPUPipelineManager &pipelineManager() { return *m_pipelineManager; }

	/**
	 * @brief Gets the render pass manager.
	 * @return Reference to render pass manager.
	 */
	RenderPassManager &renderPassManager() { return *m_renderPassManager; }

	void updateLights(const std::vector<LightStruct> &lights);

	void bindFrameUniforms(wgpu::RenderPassEncoder renderPass, uint64_t cameraId, const FrameUniforms &frameUniforms);
	void bindLightUniforms(wgpu::RenderPassEncoder renderPass);

	void renderItems(
		wgpu::CommandEncoder &encoder,
		wgpu::RenderPassEncoder renderPass,
		const RenderCollector &collector,
		const std::shared_ptr<webgpu::WebGPURenderPassContext> &renderPassContext
	);

	void renderDebugPrimitives(
		wgpu::RenderPassEncoder renderPass,
		const DebugRenderCollector &debugCollector
	);

	std::shared_ptr<webgpu::WebGPUModel> getOrCreateWebGPUModel(
		const engine::core::Handle<engine::rendering::Model> &modelHandle
	);

	void drawFullscreenQuads(
		wgpu::RenderPassEncoder renderPass,
		const std::vector<std::pair<std::shared_ptr<webgpu::WebGPUTexture>, glm::vec4>> &texturesWithViewports
	);

  private:
	std::shared_ptr<webgpu::WebGPUContext> m_context;
	std::unique_ptr<webgpu::WebGPUPipelineManager> m_pipelineManager;
	std::unique_ptr<RenderPassManager> m_renderPassManager;

	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_frameBindGroupLayout;
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_lightBindGroupLayout;
	std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> m_objectBindGroupLayout;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_lightBindGroup;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_debugBindGroup;

	std::shared_ptr<webgpu::WebGPUDepthTexture> m_depthBuffer;
	std::shared_ptr<webgpu::WebGPUTexture> m_surfaceTexture;

	std::unordered_map<Model::Handle, std::shared_ptr<webgpu::WebGPUModel>> m_modelCache;
	std::unordered_map<uint64_t, RenderTarget> m_renderTargets;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_frameBindGroupCache;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_fullscreenQuadBindGroupCache; // Cache per render target

	std::shared_ptr<webgpu::WebGPUPipeline> m_fullscreenQuadPipeline;
	wgpu::Sampler m_fullscreenQuadSampler = nullptr;
};

} // namespace engine::rendering
