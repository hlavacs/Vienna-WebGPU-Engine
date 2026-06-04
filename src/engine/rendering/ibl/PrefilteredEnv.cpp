#include "engine/rendering/ibl/PrefilteredEnv.h"

#include <array>
#include <vector>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
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
	uint32_t width,
	uint32_t height,
	uint32_t mipLevels,
	wgpu::TextureFormat format)
{
	wgpu::TextureDescriptor desc{};
	desc.label           = "PrefilteredEnv";
	desc.dimension       = wgpu::TextureDimension::_2D;
	desc.size            = {width, height, 1};
	desc.mipLevelCount   = mipLevels;
	desc.sampleCount     = 1;
	desc.format          = format;
	desc.usage           = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	desc.viewFormatCount = 0;
	desc.viewFormats     = nullptr;
	return context.getDevice().createTexture(desc);
}

wgpu::BindGroupLayout makeBindGroupLayout(webgpu::WebGPUContext &context)
{
	std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
	entries[0]                         = wgpu::Default;
	entries[0].binding                 = 0;
	entries[0].visibility              = wgpu::ShaderStage::Fragment;
	entries[0].texture.sampleType      = wgpu::TextureSampleType::Float;
	entries[0].texture.viewDimension   = wgpu::TextureViewDimension::_2D;
	entries[0].texture.multisampled    = false;
	entries[1]                         = wgpu::Default;
	entries[1].binding                 = 1;
	entries[1].visibility              = wgpu::ShaderStage::Fragment;
	entries[1].sampler.type            = wgpu::SamplerBindingType::Filtering;
	entries[2]                         = wgpu::Default;
	entries[2].binding                 = 2;
	entries[2].visibility              = wgpu::ShaderStage::Fragment;
	entries[2].buffer.type             = wgpu::BufferBindingType::Uniform;
	entries[2].buffer.minBindingSize   = sizeof(float) * 4;

	wgpu::BindGroupLayoutDescriptor desc{};
	desc.label      = "PrefilteredEnv.BindGroupLayout";
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
	desc.label                = "PrefilteredEnv.Pipeline";
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

/// One mip level's worth of per-pass state — kept together so the bake loop
/// can pin everything until the encoder submission completes.
struct MipResources
{
	wgpu::Buffer      roughnessUniform = nullptr;
	wgpu::BindGroup   bindGroup        = nullptr;
	wgpu::TextureView destinationView  = nullptr;
};

MipResources makeMipResources(
	webgpu::WebGPUContext &context,
	uint32_t mipIndex,
	uint32_t mipCount,
	const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect,
	wgpu::Sampler sampler,
	wgpu::BindGroupLayout bindGroupLayout,
	wgpu::Texture destinationTexture,
	wgpu::TextureFormat destinationFormat)
{
	MipResources out{};

	const float maxMipDenom = static_cast<float>(mipCount > 1 ? (mipCount - 1) : 1);
	const float roughness   = static_cast<float>(mipIndex) / maxMipDenom;
	const float params[4]   = {roughness, 0.0f, 0.0f, 0.0f};

	wgpu::BufferDescriptor bufDesc{};
	bufDesc.label            = "PrefilteredEnv.RoughnessUniform";
	bufDesc.size             = sizeof(params);
	bufDesc.usage            = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	bufDesc.mappedAtCreation = false;
	out.roughnessUniform = context.getDevice().createBuffer(bufDesc);
	context.getQueue().writeBuffer(out.roughnessUniform, 0, params, sizeof(params));

	std::array<wgpu::BindGroupEntry, 3> bgEntries{};
	bgEntries[0]             = wgpu::Default;
	bgEntries[0].binding     = 0;
	bgEntries[0].textureView = sourceEquirect->getTextureView();
	bgEntries[1]             = wgpu::Default;
	bgEntries[1].binding     = 1;
	bgEntries[1].sampler     = sampler;
	bgEntries[2]             = wgpu::Default;
	bgEntries[2].binding     = 2;
	bgEntries[2].buffer      = out.roughnessUniform;
	bgEntries[2].offset      = 0;
	bgEntries[2].size        = sizeof(params);

	wgpu::BindGroupDescriptor bgDesc{};
	bgDesc.label      = "PrefilteredEnv.BindGroup";
	bgDesc.layout     = bindGroupLayout;
	bgDesc.entryCount = static_cast<uint32_t>(bgEntries.size());
	bgDesc.entries    = bgEntries.data();
	out.bindGroup = context.getDevice().createBindGroup(bgDesc);

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label           = "PrefilteredEnv.MipView";
	viewDesc.format          = destinationFormat;
	viewDesc.dimension       = wgpu::TextureViewDimension::_2D;
	viewDesc.baseMipLevel    = mipIndex;
	viewDesc.mipLevelCount   = 1;
	viewDesc.baseArrayLayer  = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect          = wgpu::TextureAspect::All;
	out.destinationView = destinationTexture.createView(viewDesc);

	return out;
}

void recordBakePasses(
	wgpu::CommandEncoder &encoder,
	wgpu::RenderPipeline pipeline,
	const std::vector<MipResources> &mips)
{
	for (const auto &m : mips)
	{
		wgpu::RenderPassColorAttachment colorAttach{};
		colorAttach.view       = m.destinationView;
		colorAttach.loadOp     = wgpu::LoadOp::Clear;
		colorAttach.storeOp    = wgpu::StoreOp::Store;
		colorAttach.clearValue = wgpu::Color{0.0, 0.0, 0.0, 0.0};

		wgpu::RenderPassDescriptor rpDesc{};
		rpDesc.label                  = "PrefilteredEnv.RenderPass";
		rpDesc.colorAttachmentCount   = 1;
		rpDesc.colorAttachments       = &colorAttach;
		rpDesc.depthStencilAttachment = nullptr;

		wgpu::RenderPassEncoder pass = encoder.beginRenderPass(rpDesc);
		pass.setPipeline(pipeline);
		pass.setBindGroup(0, m.bindGroup, 0, nullptr);
		pass.draw(3, 1, 0, 0); // fullscreen triangle from vertex_index
		pass.end();
	}
}

void releaseMipResources(std::vector<MipResources> &mips)
{
	for (auto &m : mips)
	{
		if (m.destinationView)  m.destinationView.release();
		if (m.bindGroup)        m.bindGroup.release();
		if (m.roughnessUniform) { m.roughnessUniform.destroy(); m.roughnessUniform.release(); }
	}
}

} // namespace

bool PrefilteredEnv::bake(
	webgpu::WebGPUContext &context,
	const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect)
{
	if (!sourceEquirect)
	{
		spdlog::warn("PrefilteredEnv::bake: null source texture");
		return false;
	}

	const uint32_t srcWidth  = sourceEquirect->getWidth();
	const uint32_t srcHeight = sourceEquirect->getHeight();
	if (srcWidth == 0 || srcHeight == 0)
	{
		spdlog::warn("PrefilteredEnv::bake: source has zero dimensions");
		return false;
	}

	// One-shot bake: bypass PipelineManager because we don't want the LUT
	// occupying a slot in the cache (it never hot-reloads), don't need
	// auto-rebuild, and don't want to register a single-use shader against
	// the engine's canonical Frame/Scene/Material/Object bind-group layout
	// (the prefilter shader binds its own resources at @group(0)). All the
	// other primitives — texture, sampler, shader module — still go through
	// the engine factories.
	const wgpu::TextureFormat dstFormat = wgpu::TextureFormat::RGBA16Float;

	wgpu::Texture dstTexture = allocateDestinationTexture(context, srcWidth, srcHeight, MIP_LEVELS, dstFormat);
	if (!dstTexture)
	{
		spdlog::error("PrefilteredEnv: failed to allocate destination texture");
		return false;
	}

	const auto shaderPath = engine::core::PathProvider::getResource("shaders/env_prefilter.wgsl");
	wgpu::ShaderModule shaderModule = context.shaderFactory().loadShaderModule(shaderPath);
	if (!shaderModule)
	{
		spdlog::error("PrefilteredEnv: failed to load env_prefilter.wgsl");
		dstTexture.destroy();
		dstTexture.release();
		return false;
	}

	wgpu::BindGroupLayout bindGroupLayout = makeBindGroupLayout(context);
	std::array<WGPUBindGroupLayout, 1> bglRaw{static_cast<WGPUBindGroupLayout>(bindGroupLayout)};
	wgpu::PipelineLayoutDescriptor plDesc{};
	plDesc.label                = "PrefilteredEnv.PipelineLayout";
	plDesc.bindGroupLayoutCount = static_cast<uint32_t>(bglRaw.size());
	plDesc.bindGroupLayouts     = bglRaw.data();
	wgpu::PipelineLayout pipelineLayout = context.getDevice().createPipelineLayout(plDesc);

	wgpu::RenderPipeline pipeline = makePipeline(context, shaderModule, pipelineLayout, dstFormat);
	if (!pipeline)
	{
		spdlog::error("PrefilteredEnv: failed to create render pipeline");
		pipelineLayout.release();
		bindGroupLayout.release();
		shaderModule.release();
		dstTexture.destroy();
		dstTexture.release();
		return false;
	}

	auto sampler = context.samplerFactory().getClampLinearSampler();
	std::vector<MipResources> mips;
	mips.reserve(MIP_LEVELS);
	for (uint32_t mip = 0; mip < MIP_LEVELS; ++mip)
	{
		mips.push_back(makeMipResources(
			context, mip, MIP_LEVELS,
			sourceEquirect, sampler, bindGroupLayout,
			dstTexture, dstFormat));
	}

	wgpu::CommandEncoder encoder = context.createCommandEncoder("PrefilteredEnv.Encoder");
	recordBakePasses(encoder, pipeline, mips);
	context.submitCommandEncoder(encoder, "PrefilteredEnv.Commands");

	// The mip-chain texture isn't an Image-loaded texture, so we construct
	// the engine wrapper by hand rather than going through createFromHandle.
	// The default view covers every mip level — IBL consumers can sample
	// across the chain via textureSampleLevel with a roughness-driven LOD.
	wgpu::TextureDescriptor wrapTexDesc{};
	wrapTexDesc.label                   = "PrefilteredEnv";
	wrapTexDesc.dimension               = wgpu::TextureDimension::_2D;
	wrapTexDesc.size                    = {srcWidth, srcHeight, 1};
	wrapTexDesc.mipLevelCount           = MIP_LEVELS;
	wrapTexDesc.sampleCount             = 1;
	wrapTexDesc.format                  = dstFormat;
	wrapTexDesc.usage                   = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	wrapTexDesc.viewFormatCount         = 0;
	wrapTexDesc.viewFormats             = nullptr;

	wgpu::TextureViewDescriptor wrapViewDesc{};
	wrapViewDesc.label           = "PrefilteredEnv.FullView";
	wrapViewDesc.format          = dstFormat;
	wrapViewDesc.dimension       = wgpu::TextureViewDimension::_2D;
	wrapViewDesc.baseMipLevel    = 0;
	wrapViewDesc.mipLevelCount   = MIP_LEVELS;
	wrapViewDesc.baseArrayLayer  = 0;
	wrapViewDesc.arrayLayerCount = 1;
	wrapViewDesc.aspect          = wgpu::TextureAspect::All;
	wgpu::TextureView fullView   = dstTexture.createView(wrapViewDesc);

	m_texture = std::make_shared<webgpu::WebGPUTexture>(
		dstTexture,
		fullView,
		wrapTexDesc,
		wrapViewDesc,
		engine::rendering::Texture::Type::RenderTarget);

	releaseMipResources(mips);
	pipeline.release();
	pipelineLayout.release();
	bindGroupLayout.release();
	shaderModule.release();

	spdlog::info("PrefilteredEnv baked: {}x{} RGBA16Float, {} mips",
		srcWidth, srcHeight, MIP_LEVELS);
	return true;
}

} // namespace engine::rendering::ibl
