#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/RenderPass.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{
struct FrameCache;
struct RenderItemGPU;

namespace webgpu
{
class WebGPUContext;
} // namespace webgpu

/**
 * @class ForwardTransparencyPass
 * @brief Forward-shaded pass for alpha-blended geometry.
 *
 * Runs after CompositionPass + SkyboxPass over the lit HDR target with the
 * G-buffer depth read-only, and only renders items whose material has the
 * Transparent flag. Draws are sorted back-to-front per call so blending
 * composites correctly.
 *
 * Pipelines come from the shared WebGPUPipelineManager (Transparent flag
 * selects the blend-on / depth-write-off variant). Bind groups (frame, light,
 * shadow, environment) are injected by Renderer so they are not duplicated.
 */
class ForwardTransparencyPass : public RenderPass
{
  public:
	explicit ForwardTransparencyPass(std::shared_ptr<webgpu::WebGPUContext> context);
	~ForwardTransparencyPass() override = default;

	bool initialize() override;

	void setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext> &context)
	{
		m_renderPassContext = context;
	}

	void setCameraId(uint64_t id) { m_cameraId = id; }
	void setCameraPosition(const glm::vec3 &position) { m_cameraPosition = position; }

	/// Callers may pass the full visible set; the pass filters to Transparent itself.
	void setVisibleIndices(const std::vector<size_t> &indices) { m_visibleIndices = indices; }

	/// Consolidated scene bind group (lights + shadow + env + cluster). Same
	/// layout PBR forward + deferred composition share; Renderer builds the
	/// instance once per camera.
	void setSceneBindGroup(const std::shared_ptr<webgpu::WebGPUBindGroup> &bg) { m_sceneBindGroup = bg; }

	void render(FrameCache &frameCache) override;
	void cleanup() override;

  private:
	std::shared_ptr<webgpu::WebGPURenderPassContext> m_renderPassContext;
	uint64_t m_cameraId = 0;
	glm::vec3 m_cameraPosition{0.0f};
	std::vector<size_t> m_visibleIndices;

	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shaderInfo;

	// Shared scene bind group injected by Renderer per frame.
	std::shared_ptr<webgpu::WebGPUBindGroup> m_sceneBindGroup;
};

} // namespace engine::rendering
