#include "engine/rendering/ibl/PrefilteredEnv.h"

#include <array>
#include <vector>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/ibl/internal/OneShotPipeline.h"
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering::ibl
{

namespace
{

/// Allocate a multi-mip destination texture for the prefilter result.
///
/// @c WebGPUTextureFactory::createColorRenderTarget hardcodes mipLevelCount=1
/// (the common case). For the GGX prefilter we need a mip chain, so we go
/// through @c createFromDescriptors, the same factory entry point the
/// single-mip helper forwards to. That keeps texture creation flowing
/// through the factory rather than the raw device API.
std::shared_ptr<webgpu::WebGPUTexture> allocateMipChainTarget(
	webgpu::WebGPUContext &context,
	uint32_t width,
	uint32_t height,
	uint32_t mipLevels,
	wgpu::TextureFormat format)
{
	wgpu::TextureDescriptor texDesc{};
	texDesc.label                   = "PrefilteredEnv";
	texDesc.dimension               = wgpu::TextureDimension::_2D;
	texDesc.size                    = {width, height, 1};
	texDesc.mipLevelCount           = mipLevels;
	texDesc.sampleCount             = 1;
	texDesc.format                  = format;
	texDesc.usage                   = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
	texDesc.viewFormatCount         = 0;
	texDesc.viewFormats             = nullptr;

	// Default view covers every mip — IBL consumers sample across the chain
	// via textureSampleLevel with a roughness-driven LOD.
	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label           = "PrefilteredEnv.FullView";
	viewDesc.format          = format;
	viewDesc.dimension       = wgpu::TextureViewDimension::_2D;
	viewDesc.baseMipLevel    = 0;
	viewDesc.mipLevelCount   = mipLevels;
	viewDesc.baseArrayLayer  = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect          = wgpu::TextureAspect::All;

	return context.textureFactory().createFromDescriptors(
		texDesc, viewDesc, engine::rendering::Texture::Type::RenderTarget);
}

/// Bind-group layout the prefilter shader expects at @group(0):
/// 0 = source equirect, 1 = sampler, 2 = roughness uniform (vec4).
std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> buildBindGroupLayoutInfo(webgpu::WebGPUContext &context)
{
	std::vector<wgpu::BindGroupLayoutEntry> entries(3, wgpu::Default);

	entries[0].binding                 = 0;
	entries[0].visibility              = wgpu::ShaderStage::Fragment;
	entries[0].texture.sampleType      = wgpu::TextureSampleType::Float;
	entries[0].texture.viewDimension   = wgpu::TextureViewDimension::_2D;
	entries[0].texture.multisampled    = false;

	entries[1].binding                 = 1;
	entries[1].visibility              = wgpu::ShaderStage::Fragment;
	entries[1].sampler.type            = wgpu::SamplerBindingType::Filtering;

	entries[2].binding                 = 2;
	entries[2].visibility              = wgpu::ShaderStage::Fragment;
	entries[2].buffer.type             = wgpu::BufferBindingType::Uniform;
	entries[2].buffer.minBindingSize   = sizeof(float) * 4;

	// Typed binding metadata required by WebGPUBindGroupLayoutInfo's
	// constructor — the assert fires on an empty bindings vector even when
	// the wgpu-side entries are well-formed.
	std::vector<webgpu::BindGroupBinding> bindings;
	bindings.push_back({0, "srcEnv",   BindingType::Texture,       wgpu::ShaderStage::Fragment, 0, std::nullopt, std::nullopt});
	bindings.push_back({1, "srcSmp",   BindingType::Sampler,       wgpu::ShaderStage::Fragment, 0, std::nullopt, std::nullopt});
	bindings.push_back({2, "u_params", BindingType::UniformBuffer, wgpu::ShaderStage::Fragment, sizeof(float) * 4, std::nullopt, std::nullopt});

	return context.bindGroupFactory().createBindGroupLayoutInfo(
		"PrefilteredEnv.BindGroupLayout",
		BindGroupType::Custom,
		BindGroupReuse::Global,
		std::move(entries),
		std::move(bindings));
}

/// Per-mip ephemera that has to outlive the encoder submission. Kept in a
/// vector so the destination views in particular stay valid for the entire
/// bake — the encoder records pass attachments against them.
struct MipResources
{
	std::shared_ptr<webgpu::WebGPUBuffer> roughnessUniform;
	wgpu::BindGroup                       bindGroup       = nullptr;
	wgpu::TextureView                     destinationView = nullptr;
};

MipResources makeMipResources(
	webgpu::WebGPUContext               &context,
	uint32_t                             mipIndex,
	uint32_t                             mipCount,
	const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect,
	wgpu::Sampler                        sampler,
	const wgpu::BindGroupLayout         &bindGroupLayout,
	wgpu::Texture                        destinationTexture,
	wgpu::TextureFormat                  destinationFormat)
{
	MipResources out{};

	const float maxMipDenom = static_cast<float>(mipCount > 1 ? (mipCount - 1) : 1);
	const float roughness   = static_cast<float>(mipIndex) / maxMipDenom;
	const float params[4]   = {roughness, 0.0f, 0.0f, 0.0f};

	// Uniform buffer for this mip's roughness — created via the engine's
	// buffer factory so it participates in the same lifetime tracking as
	// every other UBO.
	out.roughnessUniform = context.bufferFactory().createUniformBuffer(
		"PrefilteredEnv.RoughnessUniform",
		2,                          // binding index
		params,
		4 /* element count = sizeof(vec4f) / sizeof(float) */);

	std::vector<wgpu::BindGroupEntry> entries(3, wgpu::Default);
	entries[0].binding     = 0;
	entries[0].textureView = sourceEquirect->getTextureView();
	entries[1].binding     = 1;
	entries[1].sampler     = sampler;
	entries[2].binding     = 2;
	entries[2].buffer      = out.roughnessUniform->getBuffer();
	entries[2].offset      = 0;
	entries[2].size        = sizeof(params);

	out.bindGroup = context.bindGroupFactory().createBindGroup(bindGroupLayout, entries);

	wgpu::TextureViewDescriptor viewDesc{};
	viewDesc.label           = "PrefilteredEnv.MipView";
	viewDesc.format          = destinationFormat;
	viewDesc.dimension       = wgpu::TextureViewDimension::_2D;
	viewDesc.baseMipLevel    = mipIndex;
	viewDesc.mipLevelCount   = 1;
	viewDesc.baseArrayLayer  = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect          = wgpu::TextureAspect::All;
	out.destinationView      = destinationTexture.createView(viewDesc);

	return out;
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

	// Clamp the mip count to what the source resolution supports. A scene with
	// no skybox prefilters the 1x1 default env (createFromColor); a 1x1 texture
	// allows only one mip, so requesting MIP_LEVELS=6 trips a device validation
	// error ("mip level count 6 is invalid, maximum allowed is 1").
	const uint32_t srcMaxDim = srcWidth > srcHeight ? srcWidth : srcHeight;
	uint32_t       maxMips   = 1;
	for (uint32_t d = srcMaxDim; d > 1; d >>= 1) ++maxMips;
	const uint32_t mipLevels = maxMips < MIP_LEVELS ? maxMips : MIP_LEVELS;
	m_mipLevels              = mipLevels;

	const wgpu::TextureFormat dstFormat = wgpu::TextureFormat::RGBA16Float;
	m_texture = allocateMipChainTarget(context, srcWidth, srcHeight, mipLevels, dstFormat);
	if (!m_texture)
	{
		spdlog::error("PrefilteredEnv: failed to allocate destination texture");
		return false;
	}

	auto bglInfo = buildBindGroupLayoutInfo(context);
	if (!bglInfo)
	{
		spdlog::error("PrefilteredEnv: failed to create bind group layout");
		return false;
	}

	const wgpu::BindGroupLayout &bgl = bglInfo->getLayout();
	const auto shaderPath = engine::core::PathProvider::getResource("shaders/env_prefilter.wgsl");
	auto oneShot = internal::createOneShotPipeline(
		context, shaderPath,
		&bgl, 1,
		dstFormat,
		"PrefilteredEnv");
	if (!oneShot.pipeline)
	{
		return false;
	}

	auto          samplerOwner = context.samplerFactory().getClampLinearSampler();
	wgpu::Sampler sampler      = samplerOwner ? samplerOwner->raw() : wgpu::Sampler(nullptr);
	wgpu::Texture rawDst       = m_texture->getTexture();

	std::vector<MipResources> mips;
	mips.reserve(mipLevels);
	for (uint32_t mip = 0; mip < mipLevels; ++mip)
	{
		mips.push_back(makeMipResources(
			context, mip, mipLevels,
			sourceEquirect, sampler, bgl,
			rawDst, dstFormat));
	}

	wgpu::CommandEncoder encoder = context.createCommandEncoder("PrefilteredEnv.Encoder");
	for (const auto &m : mips)
	{
		internal::recordOneShotPass(
			encoder, m.destinationView, oneShot.pipeline, m.bindGroup, "PrefilteredEnv.RenderPass");
	}
	context.submitCommandEncoder(encoder, "PrefilteredEnv.Commands");

	for (auto &m : mips)
	{
		if (m.destinationView) m.destinationView.release();
		if (m.bindGroup)       m.bindGroup.release();
		// roughnessUniform is a WebGPUBuffer shared_ptr — destruction handles release.
	}
	oneShot.release();

	spdlog::info("PrefilteredEnv baked: {}x{} RGBA16Float, {} mips",
		srcWidth, srcHeight, mipLevels);
	return true;
}

} // namespace engine::rendering::ibl
