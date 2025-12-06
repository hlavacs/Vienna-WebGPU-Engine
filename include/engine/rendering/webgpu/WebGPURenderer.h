#pragma once
#include "engine/rendering/Renderer.h"
#include "engine/scene/CameraNode.h"

#include "engine/rendering/Model.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
	class WebGPUContext;

class WebGPURenderer : public engine::rendering::Renderer
{
  public:
	WebGPURenderer(WebGPUContext *context);
	virtual ~WebGPURenderer();

	void initialize();
	void beginFrame(const engine::scene::CameraNode &camera);
	void renderScene(const engine::rendering::RenderCollector &collector);
	void submitFrame();
	void shutdown();

  private:
	WebGPUContext *m_context = nullptr;

	// GPU Model cache: CPU Handle<Model> -> GPU WebGPUModel
	std::unordered_map<engine::rendering::Model::Handle, std::shared_ptr<WebGPUModel>> m_modelCache;

	// Cache pipelines, bind groups, and uniform buffers
	std::unordered_map<size_t, wgpu::RenderPipeline> m_pipelineCache;
	std::unordered_map<size_t, wgpu::BindGroup> m_bindGroupCache;

	wgpu::Buffer m_frameUniformBuffer = nullptr;
	wgpu::Buffer m_lightsBuffer = nullptr;

	void updateFrameUniforms(const engine::scene::CameraNode &camera);
	void updateLights(const std::vector<engine::rendering::LightStruct> &lights);
};

} // namespace engine::rendering::webgpu
