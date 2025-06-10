#include "engine/rendering/webgpu/WebGPURenderer.h"

namespace engine::rendering::webgpu
{

	WebGPURenderer::WebGPURenderer()
	{
		// TODO: Create pipeline, layouts, etc.
	}
	WebGPURenderer::~WebGPURenderer()
	{
		// TODO: Cleanup
	}

	void WebGPURenderer::drawModel(const WebGPUModel &model, wgpu::RenderPassEncoder &pass)
	{
		// TODO: Bind pipeline, set buffers, bind groups, and issue draw
	}

} // namespace engine::rendering::webgpu
