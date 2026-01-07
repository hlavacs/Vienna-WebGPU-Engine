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
	auto shaderInfo = m_context->shaderRegistry().getShader(shader::default::FULLSCREEN_QUAD);
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
	const std::shared_ptr<webgpu::WebGPURenderPassContext>& renderPassContext,
	const std::shared_ptr<webgpu::WebGPUTexture>& texture
)
{
	if (!texture)
	{
		spdlog::error("CompositePass: Invalid texture");
		return;
	}

	// Create command encoder
	wgpu::CommandEncoderDescriptor encoderDesc{};
	encoderDesc.label = "CompositePass Encoder";
	wgpu::CommandEncoder encoder = m_context->getDevice().createCommandEncoder(encoderDesc);

	// Begin render pass
	wgpu::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassContext->getRenderPassDescriptor());

	// Set viewport and scissor
	auto colorTexture = renderPassContext->getColorTexture(0);
	if (colorTexture)
	{
		const uint32_t w = colorTexture->getWidth();
		const uint32_t h = colorTexture->getHeight();
		renderPass.setViewport(0.f, 0.f, float(w), float(h), 0.f, 1.f);
		renderPass.setScissorRect(0, 0, w, h);
	}

	// Bind pipeline
	renderPass.setPipeline(m_pipeline->getPipeline());

	// Get or create bind group for this texture
	uint64_t cacheKey = reinterpret_cast<uint64_t>(texture.get());
	auto it = m_bindGroupCache.find(cacheKey);
	std::shared_ptr<webgpu::WebGPUBindGroup> bindGroup;

    // ToDo: Extract into Factory
	if (it == m_bindGroupCache.end())
	{
		// Create new bind group
		auto shaderInfo = m_context->shaderRegistry().getShader(shader::default::FULLSCREEN_QUAD);
		auto bindGroupLayout = shaderInfo->getBindGroupLayout(0);
		
		std::vector<wgpu::BindGroupEntry> entries;
		entries.reserve(bindGroupLayout->getEntries().size());

		for (const auto& layoutEntry : bindGroupLayout->getEntries())
		{
			wgpu::BindGroupEntry entry{};
			entry.binding = layoutEntry.binding;

			if (layoutEntry.texture.sampleType != wgpu::TextureSampleType::Undefined)
			{
				entry.textureView = texture->getTextureView();
			}
			else if (layoutEntry.sampler.type != wgpu::SamplerBindingType::Undefined)
			{
				entry.sampler = m_sampler;
			}

			entries.push_back(entry);
		}

		wgpu::BindGroup rawBindGroup = m_context->bindGroupFactory().createBindGroup(
			bindGroupLayout->getLayout(),
			entries
		);

		bindGroup = std::make_shared<webgpu::WebGPUBindGroup>(
			rawBindGroup,
			bindGroupLayout,
			std::vector<std::shared_ptr<webgpu::WebGPUBuffer>>{}
		);
		m_bindGroupCache[cacheKey] = bindGroup;
	}
	else
	{
		bindGroup = it->second;
	}

	// Bind texture bind group (group 0)
	renderPass.setBindGroup(0, bindGroup->getBindGroup(), 0, nullptr);

	// Draw fullscreen triangle (3 vertices, no vertex buffer needed)
	renderPass.draw(3, 1, 0, 0);

	// End render pass
	renderPass.end();
	renderPass.release();

	// Submit commands
	wgpu::CommandBufferDescriptor cmdBufferDesc{};
	cmdBufferDesc.label = "CompositePass Commands";
	wgpu::CommandBuffer commands = encoder.finish(cmdBufferDesc);
	encoder.release();
	m_context->getQueue().submit(commands);
	commands.release();
}

void CompositePass::clearCache()
{
	m_bindGroupCache.clear();
}

} // namespace engine::rendering
