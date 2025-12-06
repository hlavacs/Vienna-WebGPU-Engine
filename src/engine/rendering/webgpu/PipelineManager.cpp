#include "engine/rendering/PipelineManager.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{

PipelineManager::PipelineManager(webgpu::WebGPUContext &context)
    : m_context(context)
{
}

PipelineManager::~PipelineManager()
{
    clear();
}

bool PipelineManager::createPipeline(const std::string &name, const PipelineConfig &config)
{
    if (m_pipelines.find(name) != m_pipelines.end())
    {
        spdlog::warn("Pipeline '{}' already exists, replacing", name);
        removePipeline(name);
    }

    PipelineEntry entry;
    if (!createPipelineInternal(name, config, entry))
    {
        return false;
    }

    m_pipelines[name] = std::move(entry);
    spdlog::info("Created pipeline: {}", name);
    return true;
}

std::shared_ptr<webgpu::WebGPUPipeline> PipelineManager::getPipeline(const std::string &name) const
{
    auto it = m_pipelines.find(name);
    if (it == m_pipelines.end())
    {
        spdlog::error("Pipeline '{}' not found", name);
        return nullptr;
    }
    return it->second.pipeline;
}

bool PipelineManager::reloadPipeline(const std::string &name)
{
    auto it = m_pipelines.find(name);
    if (it == m_pipelines.end())
    {
        spdlog::error("Cannot reload pipeline '{}' - not found", name);
        return false;
    }

    spdlog::info("Reloading pipeline: {}", name);

    // Wait for GPU to finish
    m_context.getQueue().submit(0, nullptr);

    // Create new pipeline with same config
    PipelineEntry newEntry;
    if (!createPipelineInternal(name, it->second.config, newEntry))
    {
        spdlog::error("Failed to reload pipeline '{}'", name);
        return false;
    }

    // Release old resources
    // WebGPUPipeline wrapper handles cleanup automatically via shared_ptr
    it->second.pipeline.reset();
    // Shader module cleanup is handled by the shared_ptr WebGPUShaderInfo
    it->second.shaderInfo.reset();

    // Replace with new
    it->second = std::move(newEntry);

    spdlog::info("Pipeline '{}' reloaded successfully", name);
    return true;
}

size_t PipelineManager::reloadAllPipelines()
{
    spdlog::info("Reloading all pipelines...");
    size_t successCount = 0;

    for (auto &[name, entry] : m_pipelines)
    {
        if (reloadPipeline(name))
        {
            successCount++;
        }
    }

    spdlog::info("Reloaded {}/{} pipelines", successCount, m_pipelines.size());
    return successCount;
}

void PipelineManager::removePipeline(const std::string &name)
{
    auto it = m_pipelines.find(name);
    if (it != m_pipelines.end())
    {
        // WebGPUPipeline wrapper handles cleanup automatically via shared_ptr
        it->second.pipeline.reset();
        it->second.shaderInfo.reset();
        m_pipelines.erase(it);
        spdlog::info("Removed pipeline: {}", name);
    }
}

void PipelineManager::clear()
{
    for (auto &[name, entry] : m_pipelines)
    {
        // WebGPUPipeline wrapper handles cleanup automatically via shared_ptr
        entry.pipeline.reset();
        entry.shaderInfo.reset();
    }
    m_pipelines.clear();
}

bool PipelineManager::createPipelineInternal(
    const std::string &name,
    const PipelineConfig &config,
    PipelineEntry &entry)
{
    // Validate shader info from config
    if (!config.shaderInfo || !config.shaderInfo->module)
    {
        spdlog::error("No shader info provided in config for pipeline '{}'", name);
        return false;
    }

    // Use the shader info directly from config
    auto shaderInfo = config.shaderInfo;

    // Create pipeline descriptor using the shader info
    wgpu::RenderPipelineDescriptor pipelineDesc = m_context.pipelineFactory().createRenderPipelineDescriptor(
        shaderInfo.get(),
        shaderInfo.get(),  // Same shader info for both vertex and fragment
        config.colorFormat,
        config.depthFormat,
        config.enableDepth
    );

    // Configure topology and vertex buffers
    pipelineDesc.primitive.topology = config.topology;
    pipelineDesc.vertex.bufferCount = config.vertexBufferCount;

    // Create pipeline layout from bind group layouts
    std::vector<wgpu::BindGroupLayout> layouts;
    layouts.reserve(config.bindGroupLayouts.size());
    for (const auto &layoutInfo : config.bindGroupLayouts)
    {
        layouts.push_back(layoutInfo->getLayout());
    }

    // Create pipeline using factory (creates layout + pipeline wrapper)
    entry.pipeline = m_context.pipelineFactory().createPipeline(
        pipelineDesc,
        layouts.data(),
        static_cast<uint32_t>(layouts.size())
    );

    if (!entry.pipeline || !entry.pipeline->isValid())
    {
        spdlog::error("Failed to create pipeline '{}'", name);
        return false;
    }

    // Store shader info and config for hot-reloading
    entry.shaderInfo = shaderInfo;
    entry.config = config;

    return true;
}

} // namespace engine::rendering