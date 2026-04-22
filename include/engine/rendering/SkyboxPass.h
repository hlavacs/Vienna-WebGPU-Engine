#pragma once

#include <memory>

#include "engine/rendering/RenderPass.h"

namespace engine::rendering
{

struct FrameCache;

namespace webgpu
{
class WebGPUContext;
class WebGPUPipeline;
class WebGPUShaderInfo;
class WebGPUBindGroup;
struct WebGPURenderPassContext;
} // namespace webgpu

/**
 * @brief Renders the environment skybox as the first camera pass.
 *
 * Uses a unit cube generated procedurally in the shader, centered at the camera,
 * with translation removed from the view matrix.
 */
class SkyboxPass : public RenderPass
{
  public:
	explicit SkyboxPass(std::shared_ptr<webgpu::WebGPUContext> context);
	~SkyboxPass() override = default;

	bool initialize() override;
	void render(FrameCache &frameCache) override;
	void cleanup() override;

	void setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext> &context)
	{
		m_renderPassContext = context;
	}

	void setCameraId(uint64_t id)
	{
		m_cameraId = id;
	}

	void setEnvironmentBindGroup(const std::shared_ptr<webgpu::WebGPUBindGroup> &bindGroup)
	{
		m_environmentBindGroup = bindGroup;
	}

  private:
	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shaderInfo;
	std::weak_ptr<webgpu::WebGPUPipeline> m_pipeline;
	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;
	std::shared_ptr<webgpu::WebGPUBindGroup> m_environmentBindGroup;
	uint64_t m_cameraId{0};
};

} // namespace engine::rendering
