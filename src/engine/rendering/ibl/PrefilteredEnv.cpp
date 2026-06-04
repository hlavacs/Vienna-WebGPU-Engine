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

bool PrefilteredEnv::bake(webgpu::WebGPUContext &context,
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

	// ---- 1. Allocate the destination texture with MIP_LEVELS mips -------
	// RGBA16Float keeps HDR headroom for the convolved env. Dimensions match
	// the source at mip 0; mip i is halved i times. Texture must allow
	// RenderAttachment so we can target individual mips, plus TextureBinding
	// so downstream IBL shaders can sample.
	const wgpu::TextureFormat dstFormat = wgpu::TextureFormat::RGBA16Float;

	wgpu::TextureDescriptor texDesc{};
	texDesc.label                   = "PrefilteredEnv";
	texDesc.dimension               = wgpu::TextureDimension::_2D;
	texDesc.size                    = {srcWidth, srcHeight, 1};
	texDesc.mipLevelCount           = MIP_LEVELS;
	texDesc.sampleCount             = 1;
	texDesc.format                  = dstFormat;
	texDesc.usage                   = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
	texDesc.viewFormatCount         = 0;
	texDesc.viewFormats             = nullptr;

	wgpu::Texture dstTexture = context.getDevice().createTexture(texDesc);
	if (!dstTexture)
	{
		spdlog::error("PrefilteredEnv: failed to allocate destination texture");
		return false;
	}

	// ---- 2. Load the shader module --------------------------------------
	const auto shaderPath = engine::core::PathProvider::getResource("shaders/env_prefilter.wgsl");
	wgpu::ShaderModule shaderModule = context.shaderFactory().loadShaderModule(shaderPath);
	if (!shaderModule)
	{
		spdlog::error("PrefilteredEnv: failed to load env_prefilter.wgsl");
		dstTexture.destroy();
		dstTexture.release();
		return false;
	}

	// ---- 3. Build the bind-group layout: src texture + sampler + uniform
	std::array<wgpu::BindGroupLayoutEntry, 3> bglEntries{};
	bglEntries[0]                         = wgpu::Default;
	bglEntries[0].binding                 = 0;
	bglEntries[0].visibility              = wgpu::ShaderStage::Fragment;
	bglEntries[0].texture.sampleType      = wgpu::TextureSampleType::Float;
	bglEntries[0].texture.viewDimension   = wgpu::TextureViewDimension::_2D;
	bglEntries[0].texture.multisampled    = false;
	bglEntries[1]                         = wgpu::Default;
	bglEntries[1].binding                 = 1;
	bglEntries[1].visibility              = wgpu::ShaderStage::Fragment;
	bglEntries[1].sampler.type            = wgpu::SamplerBindingType::Filtering;
	bglEntries[2]                         = wgpu::Default;
	bglEntries[2].binding                 = 2;
	bglEntries[2].visibility              = wgpu::ShaderStage::Fragment;
	bglEntries[2].buffer.type             = wgpu::BufferBindingType::Uniform;
	bglEntries[2].buffer.minBindingSize   = sizeof(float) * 4;

	wgpu::BindGroupLayoutDescriptor bglDesc{};
	bglDesc.label      = "PrefilteredEnv.BindGroupLayout";
	bglDesc.entryCount = static_cast<uint32_t>(bglEntries.size());
	bglDesc.entries    = bglEntries.data();
	auto bindGroupLayout = context.getDevice().createBindGroupLayout(bglDesc);

	std::array<WGPUBindGroupLayout, 1> pipelineLayouts{static_cast<WGPUBindGroupLayout>(bindGroupLayout)};
	wgpu::PipelineLayoutDescriptor plDesc{};
	plDesc.label                = "PrefilteredEnv.PipelineLayout";
	plDesc.bindGroupLayoutCount = static_cast<uint32_t>(pipelineLayouts.size());
	plDesc.bindGroupLayouts     = pipelineLayouts.data();
	auto pipelineLayout = context.getDevice().createPipelineLayout(plDesc);

	// ---- 4. Build the render pipeline -----------------------------------
	wgpu::ColorTargetState colorTarget{};
	colorTarget.format    = dstFormat;
	colorTarget.writeMask = wgpu::ColorWriteMask::All;
	colorTarget.blend     = nullptr;

	wgpu::FragmentState fragState{};
	fragState.module        = shaderModule;
	fragState.entryPoint    = "fs_main";
	fragState.constantCount = 0;
	fragState.constants     = nullptr;
	fragState.targetCount   = 1;
	fragState.targets       = &colorTarget;

	wgpu::RenderPipelineDescriptor pipeDesc{};
	pipeDesc.label                = "PrefilteredEnv.Pipeline";
	pipeDesc.layout               = pipelineLayout;
	pipeDesc.vertex.module        = shaderModule;
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

	wgpu::RenderPipeline pipeline = context.getDevice().createRenderPipeline(pipeDesc);
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

	// ---- 5. Per-mip uniform buffers + bind groups -----------------------
	// One uniform buffer per mip holding `vec4(roughness, 0, 0, 0)` — lets
	// us bake every mip in a single command encoder without writing-then-
	// reading buffer hazards.
	auto sampler = context.samplerFactory().getClampLinearSampler();

	std::vector<wgpu::Buffer>    uniforms;
	std::vector<wgpu::BindGroup> bindGroups;
	std::vector<wgpu::TextureView> dstViews;
	uniforms.reserve(MIP_LEVELS);
	bindGroups.reserve(MIP_LEVELS);
	dstViews.reserve(MIP_LEVELS);

	const float maxMipDenom = static_cast<float>(MIP_LEVELS > 1 ? (MIP_LEVELS - 1) : 1);
	for (uint32_t mip = 0; mip < MIP_LEVELS; ++mip)
	{
		const float roughness = static_cast<float>(mip) / maxMipDenom;
		const float params[4] = {roughness, 0.0f, 0.0f, 0.0f};

		wgpu::BufferDescriptor bufDesc{};
		bufDesc.label            = "PrefilteredEnv.RoughnessUniform";
		bufDesc.size             = sizeof(params);
		bufDesc.usage            = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
		bufDesc.mappedAtCreation = false;
		auto buffer = context.getDevice().createBuffer(bufDesc);
		context.getQueue().writeBuffer(buffer, 0, params, sizeof(params));
		uniforms.push_back(buffer);

		std::array<wgpu::BindGroupEntry, 3> bgEntries{};
		bgEntries[0]            = wgpu::Default;
		bgEntries[0].binding    = 0;
		bgEntries[0].textureView = sourceEquirect->getTextureView();
		bgEntries[1]            = wgpu::Default;
		bgEntries[1].binding    = 1;
		bgEntries[1].sampler    = sampler;
		bgEntries[2]            = wgpu::Default;
		bgEntries[2].binding    = 2;
		bgEntries[2].buffer     = buffer;
		bgEntries[2].offset     = 0;
		bgEntries[2].size       = sizeof(params);

		wgpu::BindGroupDescriptor bgDesc{};
		bgDesc.label      = "PrefilteredEnv.BindGroup";
		bgDesc.layout     = bindGroupLayout;
		bgDesc.entryCount = static_cast<uint32_t>(bgEntries.size());
		bgDesc.entries    = bgEntries.data();
		bindGroups.push_back(context.getDevice().createBindGroup(bgDesc));

		// View of the destination at this mip level only.
		wgpu::TextureViewDescriptor viewDesc{};
		viewDesc.label           = "PrefilteredEnv.MipView";
		viewDesc.format          = dstFormat;
		viewDesc.dimension       = wgpu::TextureViewDimension::_2D;
		viewDesc.baseMipLevel    = mip;
		viewDesc.mipLevelCount   = 1;
		viewDesc.baseArrayLayer  = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect          = wgpu::TextureAspect::All;
		dstViews.push_back(dstTexture.createView(viewDesc));
	}

	// ---- 6. Render the prefilter for each mip ---------------------------
	wgpu::CommandEncoder encoder = context.createCommandEncoder("PrefilteredEnv.Encoder");
	for (uint32_t mip = 0; mip < MIP_LEVELS; ++mip)
	{
		wgpu::RenderPassColorAttachment colorAttach{};
		colorAttach.view       = dstViews[mip];
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
		pass.setBindGroup(0, bindGroups[mip], 0, nullptr);
		pass.draw(3, 1, 0, 0);  // fullscreen triangle from vertex_index
		pass.end();
	}
	context.submitCommandEncoder(encoder, "PrefilteredEnv.Commands");

	// ---- 7. Wrap the resulting wgpu::Texture for the engine consumers ---
	// Build a default view covering every mip so downstream samplers can
	// pick the right level via textureSampleLevel(roughness * maxMip).
	wgpu::TextureViewDescriptor wholeViewDesc{};
	wholeViewDesc.label           = "PrefilteredEnv.View";
	wholeViewDesc.format           = dstFormat;
	wholeViewDesc.dimension        = wgpu::TextureViewDimension::_2D;
	wholeViewDesc.baseMipLevel     = 0;
	wholeViewDesc.mipLevelCount    = MIP_LEVELS;
	wholeViewDesc.baseArrayLayer   = 0;
	wholeViewDesc.arrayLayerCount  = 1;
	wholeViewDesc.aspect           = wgpu::TextureAspect::All;
	wgpu::TextureView wholeView    = dstTexture.createView(wholeViewDesc);

	// WebGPUTexture takes ownership of the wgpu::Texture + the default view;
	// its destructor releases both. The factory's caches don't see this one —
	// PrefilteredEnv owns the lifetime via its shared_ptr.
	m_texture = std::make_shared<webgpu::WebGPUTexture>(
		dstTexture,
		wholeView,
		texDesc,
		wholeViewDesc
	);

	// ---- 8. Release transient pipeline state ----------------------------
	for (auto &v : dstViews)    v.release();
	for (auto &bg : bindGroups) bg.release();
	for (auto &b  : uniforms)   { b.destroy(); b.release(); }
	pipeline.release();
	pipelineLayout.release();
	bindGroupLayout.release();
	shaderModule.release();

	spdlog::info("PrefilteredEnv baked: {}x{} RGBA16Float, {} mips",
		srcWidth, srcHeight, MIP_LEVELS);
	return true;
}

} // namespace engine::rendering::ibl
