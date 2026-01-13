#include "engine/rendering/CompositePass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering
{

CompositePass::CompositePass(std::shared_ptr<webgpu::WebGPUContext> context) :
	m_context(context)
{
}

bool CompositePass::initialize()
{
	spdlog::info("Initializing CompositePass");

	// Get fullscreen quad shader from registry
	auto shaderInfo = m_context->shaderRegistry().getShader(shader::default ::FULLSCREEN_QUAD);
	if (!shaderInfo || !shaderInfo->isValid())
	{
		spdlog::error("Fullscreen quad shader not found in registry");
		return false;
	}

	// Create pipeline using the shader
	m_pipeline = m_context->pipelineFactory().createRenderPipeline(
		shaderInfo,
		shaderInfo,
		m_context->surfaceManager().currentConfig().format,
		wgpu::TextureFormat::Undefined, // No depth
		Topology::Triangles,
		wgpu::CullMode::None,
		1
	);

	if (!m_pipeline || !m_pipeline->isValid())
	{
		spdlog::error("Failed to create fullscreen quad pipeline");
		return false;
	}

	// Create sampler using the sampler factory
	m_sampler = m_context->samplerFactory().getClampLinearSampler();

	spdlog::info("CompositePass initialized successfully");
	return true;
}

void CompositePass::render(
	const std::shared_ptr<webgpu::WebGPURenderPassContext> &renderPassContext,
	const std::vector<RenderTarget> &targets
)
{
	if (!renderPassContext || targets.empty())
	{
		spdlog::error("CompositePass: Invalid render pass context or empty targets");
		return;
	}

	const auto& surfaceTex = renderPassContext->getColorTexture(0);
    if (!surfaceTex)
    {
        spdlog::error("CompositePass: Render pass context has no color texture");
        return;
    }

	// --- Create command encoder once ---
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "CompositePass Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// --- Begin render pass once ---
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

	// --- Bind pipeline once ---
	renderPass.setPipeline(m_pipeline->getPipeline());


    const uint32_t surfaceW = surfaceTex->getWidth();	
    const uint32_t surfaceH = surfaceTex->getHeight();

	// --- Draw all textures ---
	 for (const auto& target : targets)
    {
        if (!target.gpuTexture)
            continue;

        // --- Compute viewport in pixels based on target.viewport [0..1] ---
        const float vx = target.viewport.x * surfaceW;
        const float vy = target.viewport.y * surfaceH;
        const float vw = target.viewport.z * surfaceW;
        const float vh = target.viewport.w * surfaceH;

        renderPass.setViewport(vx, vy, vw, vh, 0.f, 1.f);
        renderPass.setScissorRect(uint32_t(vx), uint32_t(vy), uint32_t(vw), uint32_t(vh));

        // --- Get or create bind group for this texture ---
        auto bindGroup = getOrCreateBindGroup(target.gpuTexture);
        if (!bindGroup)
        {
            spdlog::warn("CompositePass: Failed to create bind group for texture");
            continue;
        }

        // --- Bind the texture bind group (group 0) ---
        renderPass.setBindGroup(0, bindGroup->getBindGroup(), 0, nullptr);

        // --- Draw fullscreen triangle constrained by viewport ---
        renderPass.draw(3, 1, 0, 0);
    }

    // --- End render pass ---
    renderPass.end();
    renderPass.release();

    // --- Submit commands once ---
	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = "CompositePass Commands";
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
    encoder.release();
    m_context->getQueue().submit(commands);
    commands.release();
}

void CompositePass::cleanup()
{
	m_bindGroupCache.clear();
}

std::shared_ptr<webgpu::WebGPUBindGroup> CompositePass::getOrCreateBindGroup(
	const std::shared_ptr<webgpu::WebGPUTexture>& texture
)
{
	if (!texture)
		return nullptr;

	uint64_t cacheKey = reinterpret_cast<uint64_t>(texture.get());
	
	auto it = m_bindGroupCache.find(cacheKey);
	if (it != m_bindGroupCache.end())
		return it->second;

	// Create new bind group
	auto shaderInfo = m_context->shaderRegistry().getShader(shader::default::FULLSCREEN_QUAD);
	if (!shaderInfo)
		return nullptr;

	auto bindGroupLayout = shaderInfo->getBindGroupLayout(0);
	if (!bindGroupLayout)
		return nullptr;

	std::vector<wgpu::BindGroupEntry> entries;
	entries.reserve(bindGroupLayout->getEntries().size());

	for (const auto& layoutEntry : bindGroupLayout->getEntries())
	{
		wgpu::BindGroupEntry entry{};
		entry.binding = layoutEntry.binding;

		if (layoutEntry.texture.sampleType != wgpu::TextureSampleType::Undefined)
			entry.textureView = texture->getTextureView();
		else if (layoutEntry.sampler.type != wgpu::SamplerBindingType::Undefined)
			entry.sampler = m_sampler;

		entries.push_back(entry);
	}

	wgpu::BindGroup rawBindGroup = m_context->bindGroupFactory().createBindGroup(
		bindGroupLayout->getLayout(),
		entries
	);

	auto bindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
		rawBindGroup,
		bindGroupLayout,
		std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{}
	);

	m_bindGroupCache[cacheKey] = bindGroup;
	return bindGroup;
}

} // namespace engine::rendering
