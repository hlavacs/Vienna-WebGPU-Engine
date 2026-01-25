#include "engine/rendering/webgpu/WebGPUPipelineManager.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

#include <spdlog/spdlog.h>

namespace engine::rendering::webgpu
{

WebGPUPipelineManager::WebGPUPipelineManager(webgpu::WebGPUContext &context) :
	m_context(context),
	m_pipelineFactory(std::make_unique<WebGPUPipelineFactory>(context))
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
		shaderInfo->getName(),
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
	if (!createPipelineInternal(key, shaderInfo, pipeline))
	{
		spdlog::error("Failed to create pipeline for mesh '{}' and material '{}'", mesh->getName().value_or("Unnamed"), material->getName().value_or("Unnamed"));
		return nullptr;
	}
	m_pipelines[key] = pipeline;
	return pipeline;
}

std::shared_ptr<WebGPUPipeline> WebGPUPipelineManager::getOrCreatePipeline(
	const std::shared_ptr<WebGPUShaderInfo> &shaderInfo,
	wgpu::TextureFormat colorFormat,
	wgpu::TextureFormat depthFormat,
	engine::rendering::Topology::Type topology,
	wgpu::CullMode cullMode,
	uint32_t sampleCount
)
{
	PipelineKey key{
		shaderInfo->getName(),
		colorFormat,
		depthFormat,
		topology,
		cullMode,
		sampleCount
	};

	// Check cache first
	auto it = m_pipelines.find(key);
	if (it != m_pipelines.end())
	{
		return it->second;
	}

	// Create new pipeline
	std::shared_ptr<WebGPUPipeline> pipeline;
	if (!createPipelineInternal(key, shaderInfo, pipeline))
	{
		spdlog::error("Failed to create pipeline with explicit parameters");
		return nullptr;
	}

	m_pipelines[key] = pipeline;
	return pipeline;
}

bool WebGPUPipelineManager::reloadPipeline(std::shared_ptr<WebGPUPipeline> pipeline)
{
	if (!pipeline)
	{
		spdlog::warn("Cannot reload null pipeline");
		return false;
	}

	// Mark pipeline for reload after frame finishes
	m_pendingReloads.insert(pipeline);
	auto name = pipeline->getDescriptor().label ? pipeline->getDescriptor().label : "unnamed";
	spdlog::info("Pipeline '{}' marked for reload after frame finishes", name);
	return true;
}

size_t WebGPUPipelineManager::reloadAllPipelines()
{
	spdlog::info("Marking all pipelines for reload...");
	for (auto &pair : m_pipelines)
	{
		m_pendingReloads.insert(pair.second);
	}
	return m_pendingReloads.size();
}

size_t WebGPUPipelineManager::processPendingReloads()
{
	if (m_pendingReloads.empty())
		return 0;

	spdlog::info("Processing {} pending pipeline reload(s) after frame...", m_pendingReloads.size());
	size_t successCount = 0;

	// Step 1: Collect unique shaders to reload
	std::unordered_set<std::string> shadersToReload;
	for (const auto &pair : m_pipelines)
	{
		if (m_pendingReloads.find(pair.second) != m_pendingReloads.end())
			shadersToReload.insert(pair.first.shaderName);
	}

	// Step 2: Reload each shader once
	std::unordered_map<std::string, std::shared_ptr<WebGPUShaderInfo>> reloadedShaders;
	for (const auto &shaderName : shadersToReload)
	{
		auto shaderInfo = m_context.shaderRegistry().getShader(shaderName);
		if (!shaderInfo || !shaderInfo->isValid())
		{
			spdlog::error("Cannot reload shader '{}' — not found or invalid", shaderName);
			continue;
		}

		spdlog::info("Reloading shader: {}", shaderName);
		m_context.shaderFactory().reloadShader(shaderInfo);

		shaderInfo = m_context.shaderRegistry().getShader(shaderName);
		if (!shaderInfo || !shaderInfo->isValid())
		{
			spdlog::error("Failed to reload shader: {}", shaderName);
			continue;
		}

		reloadedShaders[shaderName] = shaderInfo;
	}

	// Step 3: Rebuild pipelines that use reloaded shaders
	for (auto &pair : m_pipelines)
	{
		const auto &key = pair.first;
		auto it = reloadedShaders.find(key.shaderName);
		if (it == reloadedShaders.end())
			continue; // shader not reloaded, skip

		std::shared_ptr<WebGPUPipeline> newPipeline;
		if (!createPipelineInternal(key, it->second, newPipeline))
		{
			spdlog::error("Failed to recreate pipeline for shader: {}", key.shaderName);
			continue;
		}

		pair.second = newPipeline;
		successCount++;
		spdlog::info("Pipeline reloaded successfully for shader: {}", key.shaderName);
	}

	m_pendingReloads.clear();
	spdlog::info("Completed: {}/{} pipeline(s) reloaded", successCount, m_pipelines.size());
	return successCount;
}

void WebGPUPipelineManager::cleanup()
{
	m_pipelines.clear();
}

bool WebGPUPipelineManager::createPipelineInternal(
	const PipelineKey &config,
	const std::shared_ptr<WebGPUShaderInfo> &shaderInfo,
	std::shared_ptr<webgpu::WebGPUPipeline> &outPipeline
)
{
	// Validate shader info
	if (!shaderInfo || !shaderInfo->isValid())
	{
		spdlog::error("No valid shader info provided for pipeline with shader '{}'", config.shaderName);
		return false;
	}

	outPipeline = m_pipelineFactory->createRenderPipeline(
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
		spdlog::error("Failed to create pipeline for shader '{}'", config.shaderName);
		return false;
	}
	return true;
}

} // namespace engine::rendering::webgpu