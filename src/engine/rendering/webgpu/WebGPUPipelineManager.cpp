#include "engine/rendering/webgpu/WebGPUPipelineManager.h"

#include <spdlog/spdlog.h>
#include <unordered_set>

#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipelineFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderFactory.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering::webgpu
{

WebGPUPipelineManager::WebGPUPipelineManager(webgpu::WebGPUContext &context) :
	m_context(context),
	m_pipelineFactory(std::make_unique<WebGPUPipelineFactory>(context))
{
}

WebGPUPipelineManager::~WebGPUPipelineManager()
{
	// SlotCache::cleanup() (also invoked by its destructor) snapshots every
	// slot, clears the map, then resets each slot's build_fn before
	// returning — so an outstanding Handle that lock()s after we're gone
	// gets nullptr instead of dereferencing this dead manager.
	cleanup();
}

WebGPUPipelineManager::PipelineHandle WebGPUPipelineManager::getOrCreatePipeline(
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
		(MaterialFeature::hasFlag(material->getFeatureMask(), MaterialFeature::Flag::DoubleSided))
			? wgpu::CullMode::None
			: wgpu::CullMode::Back,
		(MaterialFeature::hasFlag(material->getFeatureMask(), MaterialFeature::Flag::Transparent))
			? true
			: false,
		1 // ToDo: Get sample count from render target
	};
	return getOrCreatePipeline(
		shaderInfo,
		key.colorFormat,
		key.depthFormat,
		key.topology,
		key.cullMode,
		key.blendEnabled,
		key.sampleCount
	);
}

WebGPUPipelineManager::PipelineHandle WebGPUPipelineManager::getOrCreatePipeline(
	const std::shared_ptr<WebGPUShaderInfo> &shaderInfo,
	wgpu::TextureFormat colorFormat,
	wgpu::TextureFormat depthFormat,
	engine::rendering::Topology::Type topology,
	wgpu::CullMode cullMode,
	bool blendEnabled,
	uint32_t sampleCount
)
{
	PipelineKey key{
		shaderInfo->getName(),
		colorFormat,
		depthFormat,
		topology,
		cullMode,
		blendEnabled,
		sampleCount
	};

	// One getOrCreate covers both paths: cache hit returns the existing
	// Handle and discards the lambda; cache miss runs the lambda for the
	// initial build AND stores it as the slot's auto-rebuild function.
	// Captures `this` + key by value; the shader is looked up fresh every
	// time so hot-reloaded shaders are picked up automatically. The
	// manager's destructor calls cleanup() which resets every build_fn,
	// so dangling-this after destruction is impossible.
	return m_pipelines.getOrCreate(key, [this, key]() -> std::shared_ptr<WebGPUPipeline> {
		auto shader = m_context.shaderRegistry().getShader(key.shaderName);
		if (!shader || !shader->isValid())
		{
			spdlog::warn("Pipeline build: shader '{}' is invalid", key.shaderName);
			return nullptr;
		}
		std::shared_ptr<WebGPUPipeline> built;
		if (!createPipelineInternal(key, shader, built))
		{
			spdlog::error("Pipeline build: failed for shader '{}'", key.shaderName);
			return nullptr;
		}
		return built;
	});
}

size_t WebGPUPipelineManager::reloadAllPipelines()
{
	// 1. Reload every shader source synchronously. shaderFactory.reloadShader
	//    creates a new WebGPUShaderInfo and re-registers it via
	//    shaderRegistry.registerShader(..., replaceIfExists=true), so any
	//    subsequent getShader(name) returns the fresh one.
	std::unordered_set<std::string> shaderNames;
	for (const auto &key : m_pipelines.keys())
		shaderNames.insert(key.shaderName);

	for (const auto &name : shaderNames)
	{
		auto info = m_context.shaderRegistry().getShader(name);
		if (!info || !info->isValid())
		{
			spdlog::error("Cannot reload shader '{}' — not found or invalid", name);
			continue;
		}
		spdlog::info("Reloading shader: {}", name);
		if (!m_context.shaderFactory().reloadShader(info))
			spdlog::error("Failed to reload shader: {}", name);
	}

	// 2. Soft-clear every pipeline slot. Outstanding PipelineHandles keep
	//    working — their next lock() runs the captured build_fn, which
	//    fetches the freshly-reloaded shader from the registry and recreates
	//    the pipeline transparently. Old pipelines stay alive in any pinned
	//    lock() snapshot until the consumer drops it, which keeps in-flight
	//    GPU work safe even when reloadAllPipelines is invoked mid-frame.
	m_pipelines.clearResources();

	spdlog::info("Marked {} pipeline(s) for soft-clear; auto-rebuild on next lock()", m_pipelines.cacheSize());
	return m_pipelines.cacheSize();
}

void WebGPUPipelineManager::cleanup()
{
	m_pipelines.cleanup();
}

wgpu::BindGroup WebGPUPipelineManager::getOrCreateEmptyBindGroup()
{
	return m_pipelineFactory->getOrCreateEmptyBindGroup();
}

WebGPUPipelineFactory &WebGPUPipelineManager::factory()
{
	return *m_pipelineFactory;
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
		config.colorFormat,
		config.depthFormat,
		config.topology,
		config.cullMode,
		config.blendEnabled,
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
