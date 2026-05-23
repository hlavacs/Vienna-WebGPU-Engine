#pragma once

#include <memory>

#include "engine/rendering/RenderPass.h"

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

	/// Scene-wide light bind group (storage buffer with header + LightStruct array).
	void setSceneLightBindGroup(std::shared_ptr<webgpu::WebGPUBindGroup> bg) { m_lightBindGroup = std::move(bg); }

	/// Shadow bind group (sampler + 2D-array + cube-array + ShadowUniform array).
	void setShadowBindGroup(std::shared_ptr<webgpu::WebGPUBindGroup> bg) { m_shadowBindGroup = std::move(bg); }

	/// Cluster grid bind group (offset/count + flat light-index buffer).
	void setClusterBindGroup(std::shared_ptr<webgpu::WebGPUBindGroup> bg) { m_clusterBindGroup = std::move(bg); }

	/// Environment irradiance bind group (sampler + equirect HDR + vec4 params).
	/// Optional: when null, the composition shader's IBL branch returns zero
	/// because @c uEnvironment.params.x (irradiance enable) defaults to 0.
	void setEnvironmentBindGroup(std::shared_ptr<webgpu::WebGPUBindGroup> bg) { m_environmentBindGroup = std::move(bg); }

	/// Camera identifier for per-frame bind-group lookup in BindGroupBinder.
	void setCameraId(uint64_t id) { m_cameraId = id; }

private:
	/// Build the GBuffer bind group from the current G-buffer textures.
	bool ensureGBufferBindGroup();

	/// Build (or reuse) the cached pipeline against the active render target format.
	bool ensurePipeline();

	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shader;
	std::shared_ptr<webgpu::WebGPUPipeline> m_pipeline;

	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;
	webgpu::GBuffer *m_gBuffer = nullptr;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_gBufferBindGroup;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_lightBindGroup;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_shadowBindGroup;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_clusterBindGroup;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_environmentBindGroup;

	uint64_t m_cameraId = 0;
};

} // namespace engine::rendering
