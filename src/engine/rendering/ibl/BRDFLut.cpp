#include "engine/rendering/ibl/BRDFLut.h"

#include <array>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUTextureFactory.h"

namespace engine::rendering::ibl
{

bool BRDFLut::initialize(webgpu::WebGPUContext &context)
{
	if (m_initialized) return true;

	// ---- 1. Create the LUT texture --------------------------------------
	// RG16Float — two channels (scale + bias for the split-sum F0 term).
	// RenderAttachment so we can target it from a render pass; TextureBinding
	// so downstream shaders can sample from it.
	const wgpu::TextureFormat lutFormat = wgpu::TextureFormat::RG16Float;
	auto rawTexture = context.textureFactory().createColorRenderTarget(
		"BRDFLut",
		LUT_SIZE, LUT_SIZE,
		lutFormat,
		WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
	);
	if (!rawTexture)
	{
		spdlog::error("BRDFLut: failed to allocate render target");
		return false;
	}
	m_texture = rawTexture;

	// ---- 2. Load the shader module --------------------------------------
	const auto shaderPath = engine::core::PathProvider::getResource("shaders/brdf_lut.wgsl");
	wgpu::ShaderModule shaderModule = context.shaderFactory().loadShaderModule(shaderPath);
	if (!shaderModule)
	{
		spdlog::error("BRDFLut: failed to load brdf_lut.wgsl");
		return false;
	}

	// ---- 3. Build a minimal render pipeline -----------------------------
	// No bind groups (the shader uses pure vertex_index + math), no vertex
	// buffer, single color target. This is small enough to construct in
	// place; we don't need PipelineManager because the LUT is one-shot
	// and never hot-reloads.
	wgpu::PipelineLayoutDescriptor plDesc{};
	plDesc.bindGroupLayoutCount = 0;
	plDesc.bindGroupLayouts     = nullptr;
	plDesc.label                = "BRDFLut.PipelineLayout";
	auto pipelineLayout = context.getDevice().createPipelineLayout(plDesc);

	wgpu::ColorTargetState colorTarget{};
	colorTarget.format    = lutFormat;
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
	pipeDesc.label                = "BRDFLut.Pipeline";
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
		spdlog::error("BRDFLut: failed to create render pipeline");
		pipelineLayout.release();
		shaderModule.release();
		return false;
	}

	// ---- 4. Render the LUT ----------------------------------------------
	wgpu::CommandEncoder encoder = context.createCommandEncoder("BRDFLut.Encoder");

	wgpu::RenderPassColorAttachment colorAttach{};
	colorAttach.view       = m_texture->getTextureView();
	colorAttach.loadOp     = wgpu::LoadOp::Clear;
	colorAttach.storeOp    = wgpu::StoreOp::Store;
	colorAttach.clearValue = wgpu::Color{0.0, 0.0, 0.0, 0.0};

	wgpu::RenderPassDescriptor rpDesc{};
	rpDesc.label                = "BRDFLut.RenderPass";
	rpDesc.colorAttachmentCount = 1;
	rpDesc.colorAttachments     = &colorAttach;
	rpDesc.depthStencilAttachment = nullptr;

	wgpu::RenderPassEncoder pass = encoder.beginRenderPass(rpDesc);
	pass.setPipeline(pipeline);
	// Fullscreen triangle: 3 vertices, no vertex buffer — the shader builds
	// positions from gl_VertexIndex/@builtin(vertex_index).
	pass.draw(3, 1, 0, 0);
	pass.end();

	context.submitCommandEncoder(encoder, "BRDFLut.Commands");

	// ---- 5. Clean up transient pipeline objects -------------------------
	pipeline.release();
	pipelineLayout.release();
	shaderModule.release();

	m_initialized = true;
	spdlog::info("BRDFLut baked: {}x{} RG16Float", LUT_SIZE, LUT_SIZE);
	return true;
}

} // namespace engine::rendering::ibl
