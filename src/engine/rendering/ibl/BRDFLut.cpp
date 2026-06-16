#include "engine/rendering/ibl/BRDFLut.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/ibl/internal/OneShotPipeline.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering::ibl
{

bool BRDFLut::initialize(webgpu::WebGPUContext &context)
{
	if (m_initialized) return true;

	// One-shot bake — see OneShotPipeline.h for why the IBL bakers don't go
	// through PipelineManager. Texture/shader still flow through the
	// engine factories.
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
	auto oneShot = internal::createOneShotPipeline(
		context, shaderPath,
		nullptr, 0,           // no bind groups: shader builds result from vertex_index + math
		lutFormat,
		"BRDFLut");
	if (!oneShot.pipeline)
	{
		// Diagnostics already logged inside the helper.
		return false;
	}

	wgpu::CommandEncoder encoder = context.createCommandEncoder("BRDFLut.Encoder");
	internal::recordOneShotPass(encoder, m_texture->getTextureView(), oneShot.pipeline, nullptr, "BRDFLut.RenderPass");
	context.submitCommandEncoder(encoder, "BRDFLut.Commands");

	oneShot.release();

	m_initialized = true;
	spdlog::info("BRDFLut baked: {}x{} RG16Float", LUT_SIZE, LUT_SIZE);
	return true;
}

} // namespace engine::rendering::ibl
