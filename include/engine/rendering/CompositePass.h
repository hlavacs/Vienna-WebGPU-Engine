#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/cache/ResourceSlot.h"
#include "engine/rendering/RenderTarget.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUSampler.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

/**
 * @brief CompositePass handles final compositing of offscreen textures to the surface.
 *
 * Uses the fullscreen quad shader from ShaderRegistry.
 * Simple pass that just renders textures to surface with no depth testing.
 */
class CompositePass : public RenderPass
{
  public:
	explicit CompositePass(std::shared_ptr<webgpu::WebGPUContext> context);

	/**
	 * @brief Initialize the composite pass.
	 * @return True if initialization succeeded.
	 */
	[[nodiscard]] const char *name() const override { return "Composite (Tonemap)"; }

	bool initialize() override;

	/**
	 * @brief Set the render pass context for surface rendering.
	 * @param context Render pass context targeting the surface.
	 */
	void setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext> &context)
	{
		m_renderPassContext = context;
	}

	/**
	 * @brief Render textures to the surface using a fullscreen quad.
	 * Composites all render targets in frameCache.renderTargets to the surface.
	 * @param frameCache The frame cache containing render targets.
	 */
	void render(FrameCache &frameCache) override;

	/**
	 * @brief Clear cached bind groups.
	 */
	void cleanup() override;

	/// Toggle ACES Filmic tonemap. When off, the shader just clamps the (exposed) value.
	void setHDREnabled(bool enabled);
	[[nodiscard]] bool isHDREnabled() const { return m_hdrEnabled; }

	/// Linear exposure multiplier applied before tonemapping.
	void setExposure(float exposure);
	[[nodiscard]] float getExposure() const { return m_exposure; }

  private:
	std::shared_ptr<webgpu::WebGPUBindGroup> getOrCreateBindGroup(
		const std::shared_ptr<webgpu::WebGPUTexture> &texture,
		int layerIndex = -1
	);

	void flushPostProcessUniformsIfDirty();

	engine::rendering::cache::Handle<webgpu::WebGPUPipeline> m_pipeline;
	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shaderInfo;
	std::shared_ptr<webgpu::WebGPUSampler> m_sampler;

	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;

	std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPUBindGroup>> m_bindGroupCache;

	// Layout must match PostProcessUniforms in fullscreen_quad.wgsl.
	std::shared_ptr<webgpu::WebGPUBuffer> m_postUniformBuffer;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_postBindGroup;
	bool m_hdrEnabled{true};
	float m_exposure{1.6f};
	bool m_postDirty{true};
};

} // namespace engine::rendering
