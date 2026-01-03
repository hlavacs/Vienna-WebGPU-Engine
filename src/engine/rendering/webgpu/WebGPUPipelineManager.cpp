#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

#include <spdlog/spdlog.h>

namespace engine::rendering::webgpu
{

WebGPUPipelineManager::WebGPUPipelineManager(webgpu::WebGPUContext &context) :
	m_context(context)
{
}

WebGPUPipelineManager::~WebGPUPipelineManager()
{
	cleanup();
}

std::shared_ptr<WebGPUPipeline> WebGPUPipelineManager::getOrCreatePipeline(
	const std::shared_ptr<engine::rendering::Mesh> &mesh,
	const std::shared_ptr<engine::rendering::Material> &material,
	const std::shared_ptr<engine::rendering::webgpu::WebGPURenderPassContext> &renderPass
)
{
	auto shaderInfo = m_context.shaderRegistry().getShader(material->getShader());
	auto colorFormat = renderPass->getColorTexture(0)->getFormat();
	auto depthFormat = renderPass->getDepthTexture()->getFormat();
	PipelineKey key{
		shaderInfo,
		colorFormat,
		depthFormat,
		mesh->getTopology(),
		wgpu::CullMode::Back, // ToDo: Get cull mode from material
		1					  // ToDo: Get sample count from render target
	};
	auto it = m_pipelines.find(key);
	if (it != m_pipelines.end())
	{
		return it->second;
	}
	std::shared_ptr<WebGPUPipeline> pipeline;
	if (!createPipelineInternal(key, pipeline))
	{
		spdlog::error("Failed to create pipeline for mesh '{}' and material '{}'", mesh->getName().value_or("Unnamed"), material->getName().value_or("Unnamed"));
		return nullptr;
	}
	m_pipelines[key] = pipeline;
	return pipeline;
}

bool WebGPUPipelineManager::reloadPipeline(std::shared_ptr<WebGPUPipeline> pipeline)
{
	return true;
}

size_t WebGPUPipelineManager::reloadAllPipelines()
{
	spdlog::info("Reloading all pipelines...");
	size_t successCount = 0;
	for (auto &pair : m_pipelines)
	{
		if (reloadPipeline(pair.second))
		{
			successCount++;
		}
	}
	return successCount;
}

void WebGPUPipelineManager::cleanup()
{
	m_pipelines.clear();
}

bool WebGPUPipelineManager::createPipelineInternal(
	const PipelineKey &config,
	std::shared_ptr<webgpu::WebGPUPipeline> &outPipeline
)
{
	// Validate shader info from config
	if (!config.shaderInfo || !config.shaderInfo->isValid())
	{
		spdlog::error("No shader info provided in config for pipeline '{}'", "");
		return false;
	}
	std::shared_ptr<WebGPUShaderInfo> shaderInfo = config.shaderInfo;
	outPipeline = m_context.pipelineFactory().createRenderPipeline(
		shaderInfo,
		shaderInfo, // Same shader info for both vertex and fragment
		config.colorFormat,
		config.depthFormat,
		config.topology,
		config.cullMode,
		config.sampleCount
	);

	if (!outPipeline || !outPipeline->isValid())
	{
		spdlog::error("Failed to create pipeline '{}'", config.shaderInfo->getName());
		return false;
	}
	return true;
}

} // namespace engine::rendering::webgpu