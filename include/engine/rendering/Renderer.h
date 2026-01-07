#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/CompositePass.h"
#include "engine/rendering/DebugCollector.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/MeshPass.h"
#include "engine/rendering/Model.h"
#include "engine/rendering/RenderCollector.h"
#include "engine/rendering/RenderPassManager.h"
#include "engine/rendering/RenderingConstants.h"
#include "engine/rendering/ShadowPass.h"
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

/**
 * @brief GPU-side render item prepared for actual rendering.
 * Contains GPU resources created once and reused across multiple passes.
 */
struct RenderItemGPU
{
	std::shared_ptr<webgpu::WebGPUModel> gpuModel;
	webgpu::WebGPUMesh *gpuMesh;
	std::shared_ptr<webgpu::WebGPUMaterial> gpuMaterial;
	std::shared_ptr<webgpu::WebGPUBindGroup> objectBindGroup;
	engine::rendering::Submesh submesh;
	glm::mat4 worldTransform;
	uint32_t renderLayer;
	uint64_t objectID;
};

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

struct ShadowResources
{
	std::shared_ptr<webgpu::WebGPUTexture> shadow2DArray;
	std::shared_ptr<webgpu::WebGPUTexture> shadowCubeArray;
    std::shared_ptr<webgpu::WebGPUBuffer> shadowUniforms2D;
    std::shared_ptr<webgpu::WebGPUBuffer> shadowUniformsCube;
    wgpu::Sampler shadowSampler = nullptr;
	std::shared_ptr<webgpu::WebGPUBindGroup> bindGroup;
	bool initialized = false;
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

	void renderShadowMaps(const RenderCollector &collector);
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

	void renderDebugPrimitives(
		wgpu::RenderPassEncoder renderPass,
		const DebugRenderCollector &debugCollector
	);

	/**
	 * @brief Prepares GPU resources from CPU render items (done once per frame).
	 * @param collector The render collector with CPU-side items.
	 * @return Vector of GPU render items ready for rendering.
	 */
	std::vector<std::optional<RenderItemGPU>> prepareGPUResources(
		const RenderCollector &collector,
		const std::vector<size_t> &indicesToPrepare
	);

	/**
	 * @brief Initializes shadow pass resources.
	 * @return True if initialization succeeded.
	 */
	bool initializeShadowResources();

  private:
	std::vector<Light> m_lights;
	std::vector<RenderItemGPU> m_gpuItems;

	ShadowResources m_shadowResources;
	std::shared_ptr<webgpu::WebGPUContext> m_context;
	std::unique_ptr<webgpu::WebGPUPipelineManager> m_pipelineManager;
	std::unique_ptr<RenderPassManager> m_renderPassManager;

	// Rendering passes
	std::unique_ptr<ShadowPass> m_shadowPass;
	std::unique_ptr<MeshPass> m_meshPass;
	std::unique_ptr<CompositePass> m_compositePass;

	std::shared_ptr<webgpu::WebGPUBindGroup> m_debugBindGroup;

	std::shared_ptr<webgpu::WebGPUDepthTexture> m_depthBuffer;
	std::shared_ptr<webgpu::WebGPUTexture> m_surfaceTexture;

	std::unordered_map<uint64_t, RenderTarget> m_renderTargets;

	// GPU render items cache (prepared once per frame)
	std::vector<std::optional<RenderItemGPU>> m_gpuRenderItems;
	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_objectBindGroupCache;
};

} // namespace engine::rendering
