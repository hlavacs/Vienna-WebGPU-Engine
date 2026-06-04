#include "engine/rendering/ibl/IrradianceMap.h"

#include <array>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering::ibl
{

namespace
{

wgpu::Texture allocateDestinationTexture(
	webgpu::WebGPUContext &context,
	wgpu::TextureFormat format)
{
	wgpu::TextureDescriptor desc{};
	desc.label                   = "IrradianceMap";
	desc.dimension               = wgpu::TextureDimension::_2D;
	desc.size                    = {IrradianceMap::WIDTH, IrradianceMap::HEIGHT, 1};
	desc.mipLevelCount           = 1;
	desc.sampleCount             = 1;
	desc.format                  = format;
	desc.usage                   = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	desc.viewFormatCount         = 0;
	desc.viewFormats             = nullptr;
	return context.getDevice().createTexture(desc);
}

wgpu::BindGroupLayout makeBindGroupLayout(webgpu::WebGPUContext &context)
{
	std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
	entries[0]                       = wgpu::Default;
	entries[0].binding               = 0;
	entries[0].visibility            = wgpu::ShaderStage::Fragment;
	entries[0].texture.sampleType    = wgpu::TextureSampleType::Float;
	entries[0].texture.viewDimension = wgpu::TextureViewDimension::_2D;
	entries[0].texture.multisampled  = false;
	entries[1]                       = wgpu::Default;
	entries[1].binding               = 1;
	entries[1].visibility            = wgpu::ShaderStage::Fragment;
	entries[1].sampler.type          = wgpu::SamplerBindingType::Filtering;

	wgpu::BindGroupLayoutDescriptor desc{};
	desc.label      = "IrradianceMap.BindGroupLayout";
	desc.entryCount = static_cast<uint32_t>(entries.size());
	desc.entries    = entries.data();
	return context.getDevice().createBindGroupLayout(desc);
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
	desc.label                = "IrradianceMap.Pipeline";
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

wgpu::BindGroup makeBindGroup(
	webgpu::WebGPUContext &context,
	wgpu::BindGroupLayout layout,
	const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect,
	wgpu::Sampler sampler)
{
	std::array<wgpu::BindGroupEntry, 2> entries{};
	entries[0]             = wgpu::Default;
	entries[0].binding     = 0;
	entries[0].textureView = sourceEquirect->getTextureView();
	entries[1]             = wgpu::Default;
	entries[1].binding     = 1;
	entries[1].sampler     = sampler;

	wgpu::BindGroupDescriptor desc{};
	desc.label      = "IrradianceMap.BindGroup";
	desc.layout     = layout;
	desc.entryCount = static_cast<uint32_t>(entries.size());
	desc.entries    = entries.data();
	return context.getDevice().createBindGroup(desc);
}

void recordBakePass(
	wgpu::CommandEncoder &encoder,
	wgpu::TextureView targetView,
	wgpu::RenderPipeline pipeline,
	wgpu::BindGroup bindGroup)
{
	wgpu::RenderPassColorAttachment colorAttach{};
	colorAttach.view       = targetView;
	colorAttach.loadOp     = wgpu::LoadOp::Clear;
	colorAttach.storeOp    = wgpu::StoreOp::Store;
	colorAttach.clearValue = wgpu::Color{0.0, 0.0, 0.0, 0.0};

	wgpu::RenderPassDescriptor rpDesc{};
	rpDesc.label                  = "IrradianceMap.RenderPass";
	rpDesc.colorAttachmentCount   = 1;
	rpDesc.colorAttachments       = &colorAttach;
	rpDesc.depthStencilAttachment = nullptr;

	wgpu::RenderPassEncoder pass = encoder.beginRenderPass(rpDesc);
	pass.setPipeline(pipeline);
	pass.setBindGroup(0, bindGroup, 0, nullptr);
	pass.draw(3, 1, 0, 0); // fullscreen triangle from vertex_index
	pass.end();
}

} // namespace

bool IrradianceMap::bake(
	webgpu::WebGPUContext &context,
	const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect)
{
	if (!sourceEquirect)
	{
		spdlog::warn("IrradianceMap::bake: null source texture");
		return false;
	}

	// One-shot bake — same justification as BRDFLut/PrefilteredEnv for
	// bypassing PipelineManager (non-canonical bind groups, no hot-reload,
	// no cache benefit).
	const wgpu::TextureFormat dstFormat = wgpu::TextureFormat::RGBA16Float;

	wgpu::Texture dstTexture = allocateDestinationTexture(context, dstFormat);
	if (!dstTexture)
	{
		spdlog::error("IrradianceMap: failed to allocate destination texture");
		return false;
	}

	const auto shaderPath = engine::core::PathProvider::getResource("shaders/env_irradiance.wgsl");
	wgpu::ShaderModule shaderModule = context.shaderFactory().loadShaderModule(shaderPath);
	if (!shaderModule)
	{
		spdlog::error("IrradianceMap: failed to load env_irradiance.wgsl");
		dstTexture.destroy();
		dstTexture.release();
		return false;
	}

	wgpu::BindGroupLayout bindGroupLayout = makeBindGroupLayout(context);
	std::array<WGPUBindGroupLayout, 1> bglRaw{static_cast<WGPUBindGroupLayout>(bindGroupLayout)};
	wgpu::PipelineLayoutDescriptor plDesc{};
	plDesc.label                = "IrradianceMap.PipelineLayout";
	plDesc.bindGroupLayoutCount = static_cast<uint32_t>(bglRaw.size());
	plDesc.bindGroupLayouts     = bglRaw.data();
	wgpu::PipelineLayout pipelineLayout = context.getDevice().createPipelineLayout(plDesc);

	wgpu::RenderPipeline pipeline = makePipeline(context, shaderModule, pipelineLayout, dstFormat);
	if (!pipeline)
	{
		spdlog::error("IrradianceMap: failed to create render pipeline");
		pipelineLayout.release();
		bindGroupLayout.release();
		shaderModule.release();
		dstTexture.destroy();
		dstTexture.release();
		return false;
	}

	auto sampler = context.samplerFactory().getClampLinearSampler();
	wgpu::BindGroup bindGroup = makeBindGroup(context, bindGroupLayout, sourceEquirect, sampler);

	wgpu::TextureViewDescriptor dstViewDesc{};
	dstViewDesc.label           = "IrradianceMap.FullView";
	dstViewDesc.format          = dstFormat;
	dstViewDesc.dimension       = wgpu::TextureViewDimension::_2D;
	dstViewDesc.baseMipLevel    = 0;
	dstViewDesc.mipLevelCount   = 1;
	dstViewDesc.baseArrayLayer  = 0;
	dstViewDesc.arrayLayerCount = 1;
	dstViewDesc.aspect          = wgpu::TextureAspect::All;
	wgpu::TextureView dstView   = dstTexture.createView(dstViewDesc);

	wgpu::CommandEncoder encoder = context.createCommandEncoder("IrradianceMap.Encoder");
	recordBakePass(encoder, dstView, pipeline, bindGroup);
	context.submitCommandEncoder(encoder, "IrradianceMap.Commands");

	// Wrap as a WebGPUTexture so the renderer's bind-group plumbing accepts
	// it via the normal resource override path.
	wgpu::TextureDescriptor wrapTexDesc{};
	wrapTexDesc.label           = "IrradianceMap";
	wrapTexDesc.dimension       = wgpu::TextureDimension::_2D;
	wrapTexDesc.size            = {WIDTH, HEIGHT, 1};
	wrapTexDesc.mipLevelCount   = 1;
	wrapTexDesc.sampleCount     = 1;
	wrapTexDesc.format          = dstFormat;
	wrapTexDesc.usage           = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	wrapTexDesc.viewFormatCount = 0;
	wrapTexDesc.viewFormats     = nullptr;

	m_texture = std::make_shared<webgpu::WebGPUTexture>(
		dstTexture,
		dstView,
		wrapTexDesc,
		dstViewDesc,
		engine::rendering::Texture::Type::RenderTarget);

	bindGroup.release();
	pipeline.release();
	pipelineLayout.release();
	bindGroupLayout.release();
	shaderModule.release();

	spdlog::info("IrradianceMap baked: {}x{} RGBA16Float", WIDTH, HEIGHT);
	return true;
}

} // namespace engine::rendering::ibl
