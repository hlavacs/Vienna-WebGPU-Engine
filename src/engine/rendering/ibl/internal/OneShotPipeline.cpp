#include "engine/rendering/ibl/internal/OneShotPipeline.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"

namespace engine::rendering::ibl::internal
{

void OneShotPipeline::release()
{
	if (pipeline)       { pipeline.release();       pipeline = nullptr; }
	if (pipelineLayout) { pipelineLayout.release(); pipelineLayout = nullptr; }
	if (shaderModule)   { shaderModule.release();   shaderModule = nullptr; }
}

OneShotPipeline createOneShotPipeline(
	webgpu::WebGPUContext       &context,
	const std::filesystem::path &shaderPath,
	const wgpu::BindGroupLayout *bindGroupLayouts,
	uint32_t                     bindGroupLayoutCount,
	wgpu::TextureFormat          targetFormat,
	const char                  *label
)
{
	OneShotPipeline out{};

	out.shaderModule = context.shaderFactory().loadShaderModule(shaderPath);
	if (!out.shaderModule)
	{
		spdlog::error("OneShotPipeline: failed to load shader '{}'", shaderPath.string());
		return out;
	}

	// Pipeline layout via the factory chokepoint — keeps device.create* out of
	// the bake path like the rest of the engine.
	out.pipelineLayout = context.pipelineFactory().createPipelineLayout(
		bindGroupLayouts, bindGroupLayoutCount);

	wgpu::ColorTargetState colorTarget{};
	colorTarget.format    = targetFormat;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;
	colorTarget.blend     = nullptr;

	wgpu::FragmentState fragState{};
	fragState.module        = out.shaderModule;
	fragState.entryPoint    = "fs_main";
	fragState.constantCount = 0;
	fragState.constants     = nullptr;
	fragState.targetCount   = 1;
	fragState.targets       = &colorTarget;

	wgpu::RenderPipelineDescriptor pipeDesc{};
	pipeDesc.label                = label;
	pipeDesc.layout               = out.pipelineLayout;
	pipeDesc.vertex.module        = out.shaderModule;
	pipeDesc.vertex.entryPoint    = "vs_main";
	pipeDesc.vertex.bufferCount   = 0;
	pipeDesc.vertex.buffers       = nullptr;
	pipeDesc.primitive.topology   = wgpu::PrimitiveTopology::TriangleList;
	pipeDesc.primitive.frontFace  = wgpu::FrontFace::CCW;
	pipeDesc.primitive.cullMode   = wgpu::CullMode::None;
	pipeDesc.depthStencil         = nullptr;
	pipeDesc.multisample.count    = 1;
	pipeDesc.multisample.mask     = ~0u;
	pipeDesc.fragment             = &fragState;

	out.pipeline = context.pipelineFactory().createRenderPipeline(pipeDesc);
	if (!out.pipeline)
	{
		spdlog::error("OneShotPipeline: failed to create render pipeline '{}'", label);
		out.release();
		return out;
	}
	return out;
}

void recordOneShotPass(
	wgpu::CommandEncoder &encoder,
	wgpu::TextureView     targetView,
	wgpu::RenderPipeline  pipeline,
	wgpu::BindGroup       bindGroup,
	const char           *label
)
{
	wgpu::RenderPassColorAttachment colorAttach{};
	colorAttach.view       = targetView;
	colorAttach.loadOp     = wgpu::LoadOp::Clear;
	colorAttach.storeOp    = wgpu::StoreOp::Store;
	colorAttach.clearValue = wgpu::Color{0.0, 0.0, 0.0, 0.0};

	wgpu::RenderPassDescriptor rpDesc{};
	rpDesc.label                  = label;
	rpDesc.colorAttachmentCount   = 1;
	rpDesc.colorAttachments       = &colorAttach;
	rpDesc.depthStencilAttachment = nullptr;

	wgpu::RenderPassEncoder pass = encoder.beginRenderPass(rpDesc);
	pass.setPipeline(pipeline);
	if (bindGroup) pass.setBindGroup(0, bindGroup, 0, nullptr);
	pass.draw(3, 1, 0, 0); // fullscreen triangle from vertex_index
	pass.end();
}

} // namespace engine::rendering::ibl::internal
