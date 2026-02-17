#pragma once

#include "engine/rendering/RenderPass.h"

#include <memory>

namespace engine::rendering
{

class DebugRenderCollector;
struct FrameCache;
namespace webgpu
{
    class WebGPUContext;
    class WebGPUShaderInfo;
    class WebGPUPipeline;
    class WebGPUBindGroup;
    struct WebGPURenderPassContext;
} // namespace webgpu

class DebugPass : public RenderPass
{
  public:
	explicit DebugPass(std::shared_ptr<webgpu::WebGPUContext> context);
	~DebugPass() override = default;

	bool initialize() override;
	void render(FrameCache &frameCache) override;
	void cleanup() override;

	// Set the debug collector for this pass
	void setDebugCollector(const DebugRenderCollector *collector);

	void setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext>& renderPassContext);

	/**
	 * @brief Set the camera ID for bind group caching.
	 * @param id Camera identifier.
	 */
	void setCameraId(uint64_t id)
	{
		m_cameraId = id;
	}

  private:
	const DebugRenderCollector *m_debugCollector = nullptr;
	uint64_t m_cameraId = 0;

	// Pipeline, shader, and sampler for debug rendering
	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shaderInfo;
	std::weak_ptr<webgpu::WebGPUPipeline> m_pipeline;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_debugBindGroup;
	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;

	wgpu::Sampler m_sampler = nullptr;
};

} // namespace engine::rendering