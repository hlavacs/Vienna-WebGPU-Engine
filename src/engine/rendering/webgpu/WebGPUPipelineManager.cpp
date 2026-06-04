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
	// Break any captured `this` in slot build_fns before tearing down — an
	// outstanding Handle that lock()s after our destructor runs would
	// otherwise dereference a dead manager. After this point, lock() returns
	// nullptr for evicted slots (build_fn is gone).
	for (auto &pair : m_pipelines)
	{
		if (pair.second) pair.second->resetBuildFn();
	}
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

	// Cache hit: hand out a Handle backed by the existing slot. Reloads land
	// inside the slot, so any handle created here will pick up the new
	// pipeline transparently next frame.
	auto it = m_pipelines.find(key);
	if (it != m_pipelines.end())
	{
		return PipelineHandle{it->second};
	}

	// Cache miss: build the pipeline, wrap it in a new slot, store the slot.
	std::shared_ptr<WebGPUPipeline> pipeline;
	if (!createPipelineInternal(key, shaderInfo, pipeline))
	{
		spdlog::error("Failed to create pipeline with explicit parameters");
		return {};
	}

	// build_fn lets evict() + lock() auto-rebuild. Captures `this` + key by
	// value; the shader name is looked up fresh every time so hot-reloaded
	// shaders are picked up automatically. The manager's destructor resets
	// build_fn on every slot before tearing down, so dangling-this is
	// impossible.
	auto buildFn = [this, key]() -> std::shared_ptr<WebGPUPipeline> {
		auto shader = m_context.shaderRegistry().getShader(key.shaderName);
		if (!shader || !shader->isValid())
		{
			spdlog::warn("Auto-rebuild: shader '{}' is no longer valid", key.shaderName);
			return nullptr;
		}
		std::shared_ptr<WebGPUPipeline> rebuilt;
		if (!createPipelineInternal(key, shader, rebuilt))
		{
			spdlog::error("Auto-rebuild: failed for shader '{}'", key.shaderName);
			return nullptr;
		}
		return rebuilt;
	};

	auto slot = std::make_shared<PipelineSlot>(std::move(pipeline), std::move(buildFn), &m_frameCounter);
	m_pipelines.emplace(key, slot);
	return PipelineHandle{slot};
}

std::size_t WebGPUPipelineManager::aliveCount() const
{
	std::size_t alive = 0;
	for (const auto &pair : m_pipelines)
	{
		if (pair.second && pair.second->isAlive()) ++alive;
	}
	return alive;
}

std::size_t WebGPUPipelineManager::evictStale()
{
	if (m_maxIdleFrames == 0) return 0; // never-evict mode

	const uint32_t now = m_frameCounter.load(std::memory_order_relaxed);
	std::size_t evicted = 0;
	for (auto &pair : m_pipelines)
	{
		if (!pair.second || !pair.second->isAlive()) continue;
		const uint32_t last = pair.second->lastAccessFrame();
		// Unsigned subtraction handles wraparound correctly as long as the
		// total range never exceeds 2^31 frames between accesses (years).
		if ((now - last) > m_maxIdleFrames)
		{
			pair.second->evict();
			++evicted;
		}
	}
	if (evicted > 0)
		spdlog::debug("PipelineManager: evicted {} stale pipeline(s) (window = {} frames)", evicted, m_maxIdleFrames);
	return evicted;
}

size_t WebGPUPipelineManager::reloadAllPipelines()
{
	spdlog::info("Marking all pipelines for reload...");
	m_pendingReloads.clear();
	for (const auto &pair : m_pipelines)
	{
		m_pendingReloads.insert(pair.first);
	}
	return m_pendingReloads.size();
}

size_t WebGPUPipelineManager::processPendingReloads()
{
	if (m_pendingReloads.empty())
		return 0;

	spdlog::info("Processing {} pending pipeline reload(s) after frame...", m_pendingReloads.size());
	size_t successCount = 0;

	// Step 1: Collect unique shaders touched by pending reloads.
	std::unordered_set<std::string> shadersToReload;
	for (const auto &key : m_pendingReloads)
	{
		shadersToReload.insert(key.shaderName);
	}

	// Step 2: Reload each shader once. Skip pipelines whose shader fails.
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

	// Step 3: Rebuild pipelines whose shader was successfully reloaded.
	// Slot->replace() swaps the resource in place; outstanding handles
	// pick up the new pipeline on their next lock() call.
	for (const auto &key : m_pendingReloads)
	{
		auto shaderIt = reloadedShaders.find(key.shaderName);
		if (shaderIt == reloadedShaders.end())
			continue; // Shader reload failed; keep the existing pipeline live.

		auto slotIt = m_pipelines.find(key);
		if (slotIt == m_pipelines.end())
			continue; // Entry was evicted between mark and process — fine, just skip.

		std::shared_ptr<WebGPUPipeline> newPipeline;
		if (!createPipelineInternal(key, shaderIt->second, newPipeline))
		{
			spdlog::error("Failed to recreate pipeline for shader: {}", key.shaderName);
			continue;
		}

		// Discard the returned previous pipeline — any in-flight handle that
		// pinned the old one still owns it via its own shared_ptr.
		(void)slotIt->second->replace(std::move(newPipeline));
		++successCount;
		spdlog::info("Pipeline reloaded successfully for shader: {}", key.shaderName);
	}

	m_pendingReloads.clear();
	spdlog::info("Completed: {}/{} pipeline(s) reloaded", successCount, m_pipelines.size());
	return successCount;
}

void WebGPUPipelineManager::cleanup()
{
	m_pipelines.clear();
	m_pendingReloads.clear();
}

wgpu::BindGroup WebGPUPipelineManager::getOrCreateEmptyBindGroup()
{
	return m_pipelineFactory->getOrCreateEmptyBindGroup();
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
