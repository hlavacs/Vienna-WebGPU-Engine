#pragma once

#include <memory>

#include "engine/rendering/RenderPass.h"
#include "engine/rendering/cache/ResourceSlot.h"

namespace engine::rendering::webgpu
{
class GBuffer;
class WebGPUBindGroup;
class WebGPUPipeline;
// WebGPURenderPassContext is declared as a struct - matching declaration here
// keeps MSVC name mangling consistent (class vs struct mangles differently).
struct WebGPURenderPassContext;
class WebGPUShaderInfo;
class WebGPUTexture;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

struct FrameCache;

/**
 * @class CompositionPass
 * @brief Final lighting pass of the deferred renderer.
 *
 * Reads the four G-buffer textures plus the scene-wide light, shadow and
 * cluster bind groups, runs a single full-screen triangle through the
 * @c shader::defaults::COMPOSITION_DEFERRED shader, and writes the lit
 * result into the camera's HDR intermediate render target.
 *
 * All GPU resources flow through factories:
 *  - Pipeline: @ref webgpu::WebGPUPipelineManager::getOrCreatePipeline (cached)
 *  - GBuffer bind group: @ref webgpu::WebGPUBindGroupFactory::createBindGroup,
 *    built once and rebuilt only when the underlying G-buffer textures change
 *    (signalled by @ref cleanup ).
 *  - Frame / Light / Shadow / Cluster bind groups: supplied by their owning
 *    systems (FrameCache, SceneLightBuffer, ShadowPass, ClusterManager).
 *
 * Binding dispatch goes through @ref BindGroupBinder so indices are resolved
 * by name from the shader's reflection data - the pass never hardcodes a
 * group index.
 */
class CompositionPass : public RenderPass
{
public:
	explicit CompositionPass(std::shared_ptr<webgpu::WebGPUContext> context);
	~CompositionPass() override = default;

	CompositionPass(const CompositionPass &) = delete;
	CompositionPass &operator=(const CompositionPass &) = delete;

	/**
	 * @brief Resolves the composition shader from the registry.
	 * @return true on success, false if the shader is missing or invalid.
	 */
	bool initialize() override;

	/**
	 * @brief Renders one camera's composed image.
	 * Expects all setters below to have been called for the current frame.
	 */
	void render(FrameCache &frameCache) override;

	/**
	 * @brief Invalidates cached state that depends on external GPU resources.
	 *
	 * Drops the pipeline (e.g. after target-format change or hot-reload) and
	 * the G-buffer bind group (e.g. after G-buffer resize). The next render
	 * call rebuilds whatever is missing.
	 */
	void cleanup() override;

	/// Target the camera's HDR intermediate texture (cleared by the renderer).
	void setRenderPassContext(std::shared_ptr<webgpu::WebGPURenderPassContext> ctx) { m_renderPassContext = std::move(ctx); }

	/// G-buffer to read from. Setting a different G-buffer invalidates the cached bind group.
	void setGBuffer(webgpu::GBuffer *gBuffer);

	/// Consolidated scene bind group: @binding(0) lights, @1-4 shadow, @5-7
	/// environment, @8-9 cluster. Renderer::updateSceneBindGroup builds it once
	/// per camera; the forward transparency pass shares the same instance.
	void setSceneBindGroup(std::shared_ptr<webgpu::WebGPUBindGroup> bg) { m_sceneBindGroup = std::move(bg); }

	/// Camera identifier for per-frame bind-group lookup in BindGroupBinder.
	void setCameraId(uint64_t id) { m_cameraId = id; }

private:
	/// Build the GBuffer bind group from the current G-buffer textures.
	bool ensureGBufferBindGroup();

	/// Build (or reuse) the cached pipeline against the active render target format.
	bool ensurePipeline();

	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shader;
	// Pipeline lives in the PipelineManager's cache; we hold a Handle so a
	// hot-reload swap inside the manager is picked up here automatically.
	engine::rendering::cache::Handle<webgpu::WebGPUPipeline> m_pipeline;

	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;
	webgpu::GBuffer *m_gBuffer = nullptr;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_gBufferBindGroup;
	// First-color-texture identity captured when m_gBufferBindGroup was built.
	// GBuffer::resize replaces every color texture, so a mismatch here means the
	// cached bind group is sampling destroyed views and must be rebuilt - even
	// when the GBuffer pointer is unchanged (the common case for in-frame resizes
	// like multi-camera with different viewports, where Renderer::onResize never fires).
	const webgpu::WebGPUTexture *m_gBufferBindGroupFingerprint = nullptr;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_sceneBindGroup;

	uint64_t m_cameraId = 0;
};

} // namespace engine::rendering
