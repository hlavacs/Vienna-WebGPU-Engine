#include "engine/rendering/ibl/BRDFLut.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering::ibl
{

namespace
{

wgpu::PipelineLayout makeEmptyPipelineLayout(webgpu::WebGPUContext &context)
{
	// No bind groups — the LUT shader builds its result from vertex_index
	// + math, no external inputs.
	wgpu::PipelineLayoutDescriptor desc{};
	desc.bindGroupLayoutCount = 0;
	desc.bindGroupLayouts     = nullptr;
	desc.label                = "BRDFLut.PipelineLayout";
	return context.getDevice().createPipelineLayout(desc);
}

wgpu::RenderPipeline makePipeline(
	webgpu::WebGPUContext &context,
	wgpu::ShaderModule shaderModule,
	wgpu::PipelineLayout pipelineLayout,
	wgpu::TextureFormat targetFormat)
{
	wgpu::ColorTargetState colorTarget{};
	colorTarget.format    = targetFormat;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;
	colorTarget.blend     = nullptr;

	wgpu::FragmentState fragState{};
	fragState.module        = shaderModule;
	fragState.entryPoint    = "fs_main";
	fragState.constantCount = 0;
	fragState.constants     = nullptr;
	fragState.targetCount   = 1;
	fragState.targets       = &colorTarget;

	wgpu::RenderPipelineDescriptor desc{};
	desc.label                = "BRDFLut.Pipeline";
	desc.layout               = pipelineLayout;
	desc.vertex.module        = shaderModule;
	desc.vertex.entryPoint    = "vs_main";
	desc.vertex.bufferCount   = 0;
	desc.vertex.buffers       = nullptr;
	desc.primitive.topology   = wgpu::PrimitiveTopology::TriangleList;
	desc.primitive.frontFace  = wgpu::FrontFace::CCW;
	desc.primitive.cullMode   = wgpu::CullMode::None;
	desc.depthStencil         = nullptr;
	desc.multisample.count    = 1;
	desc.multisample.mask     = ~0u;
	desc.fragment             = &fragState;
	return context.getDevice().createRenderPipeline(desc);
}

void recordBakePass(
	wgpu::CommandEncoder &encoder,
	wgpu::TextureView targetView,
	wgpu::RenderPipeline pipeline)
{
	wgpu::RenderPassColorAttachment colorAttach{};
	colorAttach.view       = targetView;
	colorAttach.loadOp     = wgpu::LoadOp::Clear;
	colorAttach.storeOp    = wgpu::StoreOp::Store;
	colorAttach.clearValue = wgpu::Color{0.0, 0.0, 0.0, 0.0};

	wgpu::RenderPassDescriptor rpDesc{};
	rpDesc.label                  = "BRDFLut.RenderPass";
	rpDesc.colorAttachmentCount   = 1;
	rpDesc.colorAttachments       = &colorAttach;
	rpDesc.depthStencilAttachment = nullptr;

	wgpu::RenderPassEncoder pass = encoder.beginRenderPass(rpDesc);
	pass.setPipeline(pipeline);
	pass.draw(3, 1, 0, 0); // fullscreen triangle from vertex_index
	pass.end();
}

} // namespace

bool BRDFLut::initialize(webgpu::WebGPUContext &context)
{
	if (m_initialized) return true;

	// One-shot bake: bypass PipelineManager because the LUT never hot-reloads
	// and never participates in the per-pass orchestration. Texture, sampler,
	// shader module still go through the engine factories.
	const wgpu::TextureFormat lutFormat = wgpu::TextureFormat::RG16Float;
	m_texture = context.textureFactory().createColorRenderTarget(
		"BRDFLut",
		LUT_SIZE, LUT_SIZE,
		lutFormat,
		WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);
	if (!m_texture)
	{
		spdlog::error("BRDFLut: failed to allocate render target");
		return false;
	}

	const auto shaderPath = engine::core::PathProvider::getResource("shaders/brdf_lut.wgsl");
	wgpu::ShaderModule shaderModule = context.shaderFactory().loadShaderModule(shaderPath);
	if (!shaderModule)
	{
		spdlog::error("BRDFLut: failed to load brdf_lut.wgsl");
		return false;
	}

	wgpu::PipelineLayout pipelineLayout = makeEmptyPipelineLayout(context);
	wgpu::RenderPipeline pipeline = makePipeline(context, shaderModule, pipelineLayout, lutFormat);
	if (!pipeline)
	{
		spdlog::error("BRDFLut: failed to create render pipeline");
		pipelineLayout.release();
		shaderModule.release();
		return false;
	}

	wgpu::CommandEncoder encoder = context.createCommandEncoder("BRDFLut.Encoder");
	recordBakePass(encoder, m_texture->getTextureView(), pipeline);
	context.submitCommandEncoder(encoder, "BRDFLut.Commands");

	pipeline.release();
	pipelineLayout.release();
	shaderModule.release();

	m_initialized = true;
	spdlog::info("BRDFLut baked: {}x{} RG16Float", LUT_SIZE, LUT_SIZE);
	return true;
}

} // namespace engine::rendering::ibl
