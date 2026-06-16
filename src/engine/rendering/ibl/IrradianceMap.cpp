#include "engine/rendering/ibl/IrradianceMap.h"

#include <array>
#include <vector>

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/Texture.h"
#include "engine/rendering/ibl/internal/OneShotPipeline.h"
#include "engine/rendering/BindGroupEnums.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering::ibl
{

namespace
{

/// Bind-group layout entries the convolution shader expects at @group(0):
/// 0 = source equirect texture, 1 = sampler. Built once per bake; the
/// engine's BindGroupFactory wraps the wgpu handle in WebGPUBindGroupLayoutInfo.
std::shared_ptr<webgpu::WebGPUBindGroupLayoutInfo> buildBindGroupLayoutInfo(webgpu::WebGPUContext &context)
{
	std::vector<wgpu::BindGroupLayoutEntry> entries(2, wgpu::Default);

	entries[0].binding                 = 0;
	entries[0].visibility              = wgpu::ShaderStage::Fragment;
	entries[0].texture.sampleType      = wgpu::TextureSampleType::Float;
	entries[0].texture.viewDimension   = wgpu::TextureViewDimension::_2D;
	entries[0].texture.multisampled    = false;

	entries[1].binding                 = 1;
	entries[1].visibility              = wgpu::ShaderStage::Fragment;
	entries[1].sampler.type            = wgpu::SamplerBindingType::Filtering;

	// Typed binding metadata so WebGPUBindGroupLayoutInfo can answer
	// reflection queries (lookup by name, by slot, etc.). The layout's
	// assertion fires if this is empty.
	std::vector<webgpu::BindGroupBinding> bindings;
	bindings.push_back({0, "srcEnv", BindingType::Texture, wgpu::ShaderStage::Fragment, 0, std::nullopt, std::nullopt});
	bindings.push_back({1, "srcSmp", BindingType::Sampler, wgpu::ShaderStage::Fragment, 0, std::nullopt, std::nullopt});

	return context.bindGroupFactory().createBindGroupLayoutInfo(
		"IrradianceMap.BindGroupLayout",
		BindGroupType::Custom,
		BindGroupReuse::Global,
		std::move(entries),
		std::move(bindings));
}

/// Bind-group entries for the convolution pass: source texture view + sampler.
wgpu::BindGroup buildBindGroup(
	webgpu::WebGPUContext &context,
	const wgpu::BindGroupLayout &layout,
	const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect,
	wgpu::Sampler sampler)
{
	std::vector<wgpu::BindGroupEntry> entries(2, wgpu::Default);
	entries[0].binding     = 0;
	entries[0].textureView = sourceEquirect->getTextureView();
	entries[1].binding     = 1;
	entries[1].sampler     = sampler;
	return context.bindGroupFactory().createBindGroup(layout, entries);
}

/// Build a WebGPUTexture wrapper for the baked destination so it slots into
/// the engine's bind-group plumbing alongside any other shared_ptr<WebGPUTexture>.
std::shared_ptr<webgpu::WebGPUTexture> wrapDestination(
	webgpu::WebGPUContext &context,
	wgpu::TextureFormat format,
	uint32_t width,
	uint32_t height)
{
	// Single-mip render target with both attachment + sampler-binding
	// usage. The texture factory's createColorRenderTarget already does
	// exactly this — use it and skip the manual descriptor.
	return context.textureFactory().createColorRenderTarget(
		"IrradianceMap",
		width, height,
		format,
		WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);
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

	const wgpu::TextureFormat dstFormat = wgpu::TextureFormat::RGBA16Float;
	m_texture = wrapDestination(context, dstFormat, WIDTH, HEIGHT);
	if (!m_texture)
	{
		spdlog::error("IrradianceMap: failed to allocate destination texture");
		return false;
	}

	auto bglInfo = buildBindGroupLayoutInfo(context);
	if (!bglInfo)
	{
		spdlog::error("IrradianceMap: failed to create bind group layout");
		return false;
	}

	const wgpu::BindGroupLayout &bgl = bglInfo->getLayout();
	const auto shaderPath = engine::core::PathProvider::getResource("shaders/env_irradiance.wgsl");
	auto oneShot = internal::createOneShotPipeline(
		context, shaderPath,
		&bgl, 1,
		dstFormat,
		"IrradianceMap");
	if (!oneShot.pipeline)
	{
		return false;
	}

	auto            samplerOwner = context.samplerFactory().getClampLinearSampler();
	wgpu::Sampler   sampler      = samplerOwner ? samplerOwner->raw() : wgpu::Sampler(nullptr);
	wgpu::BindGroup bindGroup    = buildBindGroup(context, bgl, sourceEquirect, sampler);

	wgpu::CommandEncoder encoder = context.createCommandEncoder("IrradianceMap.Encoder");
	internal::recordOneShotPass(encoder, m_texture->getTextureView(), oneShot.pipeline, bindGroup, "IrradianceMap.RenderPass");
	context.submitCommandEncoder(encoder, "IrradianceMap.Commands");

	bindGroup.release();
	oneShot.release();

	spdlog::info("IrradianceMap baked: {}x{} RGBA16Float", WIDTH, HEIGHT);
	return true;
}

} // namespace engine::rendering::ibl
