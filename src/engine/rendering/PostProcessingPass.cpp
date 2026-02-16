#include "engine/rendering/PostProcessingPass.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/FrameCache.h"
#include "engine/rendering/RenderTarget.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering
{

PostProcessingPass::PostProcessingPass(std::shared_ptr<webgpu::WebGPUContext> context) :
	RenderPass(context)
{
}

bool PostProcessingPass::initialize()
{
}

void PostProcessingPass::setInputTexture(const std::shared_ptr<webgpu::WebGPUTexture> &texture)
{
	m_inputTexture = texture;
}

void PostProcessingPass::setRenderPassContext(const std::shared_ptr<webgpu::WebGPURenderPassContext> &renderPassContext)
{
	m_renderPassContext = renderPassContext;
}

void PostProcessingPass::render(FrameCache &frameCache)
{
}

std::shared_ptr<webgpu::WebGPUBindGroup> PostProcessingPass::getOrCreateBindGroup(
	const std::shared_ptr<webgpu::WebGPUTexture> &texture,
	int layerIndex
)
{
}

void PostProcessingPass::cleanup()
{
	// Tutorial 4 - Step 6: Release bind group cache
	// Called on shutdown or window resize
	// The pipeline and sampler are managed by pipeline manager and sampler factory
	// We only need to clear bind group cache
}

} // namespace engine::rendering
