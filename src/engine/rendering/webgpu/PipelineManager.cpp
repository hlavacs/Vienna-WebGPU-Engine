#include "engine/rendering/PipelineManager.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

namespace engine::rendering
{

PipelineManager::PipelineManager(webgpu::WebGPUContext &context)
    : ResourceManagerBase<webgpu::WebGPUPipeline>(),  // Initialize base (sets up resolver)
      m_context(context)
{
}

PipelineManager::~PipelineManager()
{
    clear();
}

bool PipelineManager::createPipeline(const std::string &name, const PipelineConfig &config)
{
    // Check if pipeline with this name already exists
    auto it = m_nameToHandle.find(name);
    if (it != m_nameToHandle.end())
    {
        spdlog::warn("Pipeline '{}' already exists, replacing", name);
        removePipeline(name);
    }

    // Create the pipeline
    std::shared_ptr<webgpu::WebGPUPipeline> pipeline;
    
    if (!createPipelineInternal(name, config, pipeline))
    {
        return false;
    }

    // Register with base ResourceManagerBase (sets up handle resolution)
    auto handleOpt = add(pipeline);
    if (!handleOpt)
    {
        spdlog::error("Failed to register pipeline '{}' with base manager", name);
        return false;
    }

    auto handle = handleOpt.value();
    
    // Store name-to-handle mapping
    m_nameToHandle[name] = handle;
    
    // Store config for reloading (shader info is in the pipeline itself)
    m_configs[handle] = config;

    spdlog::info("Created pipeline: {}", name);
    return true;
}

std::shared_ptr<webgpu::WebGPUPipeline> PipelineManager::getPipeline(const std::string &name) const
{
    auto it = m_nameToHandle.find(name);
    if (it == m_nameToHandle.end())
    {
        spdlog::error("Pipeline '{}' not found", name);
        return nullptr;
    }
    
    // Use base class get() method with handle
    auto pipelineOpt = get(it->second);
    if (!pipelineOpt)
    {
        spdlog::error("Pipeline '{}' handle is invalid", name);
        return nullptr;
    }
    
    return pipelineOpt.value();
}

std::shared_ptr<webgpu::WebGPUShaderInfo> PipelineManager::getShaderInfo(const std::string &name) const
{
    auto it = m_nameToHandle.find(name);
    if (it == m_nameToHandle.end())
    {
        return nullptr;
    }
    
    // Get pipeline from base class
    auto pipelineOpt = get(it->second);
    if (!pipelineOpt)
    {
        return nullptr;
    }
    
    // Return shader info from the pipeline itself
    return pipelineOpt.value()->getShaderInfo();
}

bool PipelineManager::reloadPipeline(const std::string &name)
{
    auto it = m_nameToHandle.find(name);
    if (it == m_nameToHandle.end())
    {
        spdlog::error("Cannot reload pipeline '{}' - not found", name);
        return false;
    }

    spdlog::info("Reloading pipeline: {}", name);

    auto handle = it->second;
    auto configIt = m_configs.find(handle);
    if (configIt == m_configs.end())
    {
        spdlog::error("Cannot reload pipeline '{}' - no config found", name);
        return false;
    }

    // Wait for GPU to finish
    m_context.getQueue().submit(0, nullptr);

    // Create new pipeline with same config
    std::shared_ptr<webgpu::WebGPUPipeline> newPipeline;
    
    if (!createPipelineInternal(name, configIt->second, newPipeline))
    {
        spdlog::error("Failed to reload pipeline '{}'", name);
        return false;
    }

    // Remove old pipeline from base manager
    remove(handle);
    m_configs.erase(configIt);

    // Register new pipeline with base manager
    auto newHandleOpt = add(newPipeline);
    if (!newHandleOpt)
    {
        spdlog::error("Failed to register reloaded pipeline '{}'", name);
        return false;
    }

    auto newHandle = newHandleOpt.value();
    
    // Update mappings
    m_nameToHandle[name] = newHandle;
    m_configs[newHandle] = configIt->second;

    spdlog::info("Pipeline '{}' reloaded successfully", name);
    return true;
}

size_t PipelineManager::reloadAllPipelines()
{
    spdlog::info("Reloading all pipelines...");
    size_t successCount = 0;

    // Copy names to avoid iterator invalidation during reload
    std::vector<std::string> names;
    names.reserve(m_nameToHandle.size());
    for (const auto &[name, _] : m_nameToHandle)
    {
        names.push_back(name);
    }

    for (const auto &name : names)
    {
        if (reloadPipeline(name))
        {
            successCount++;
        }
    }

    spdlog::info("Reloaded {}/{} pipelines", successCount, names.size());
    return successCount;
}

void PipelineManager::removePipeline(const std::string &name)
{
    auto it = m_nameToHandle.find(name);
    if (it != m_nameToHandle.end())
    {
        auto handle = it->second;
        
        // Remove from base ResourceManagerBase
        remove(handle);
        
        // Remove config
        m_configs.erase(handle);
        
        // Remove name mapping
        m_nameToHandle.erase(it);
        
        spdlog::info("Removed pipeline: {}", name);
    }
}

void PipelineManager::clear()
{
    // Clear name mappings
    m_nameToHandle.clear();
    
    // Clear configs
    m_configs.clear();
    
    // Clear base class resources (this will invalidate handles and release pipelines)
    ResourceManagerBase<webgpu::WebGPUPipeline>::clear();
}

bool PipelineManager::createPipelineInternal(
    const std::string &name,
    const PipelineConfig &config,
    std::shared_ptr<webgpu::WebGPUPipeline> &outPipeline)
{
    // Validate shader info from config
    if (!config.shaderInfo || !config.shaderInfo->isValid())
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
    // Pass shader info so it's stored in the WebGPUPipeline
    outPipeline = m_context.pipelineFactory().createPipeline(
        pipelineDesc,
        layouts.data(),
        static_cast<uint32_t>(layouts.size()),
        shaderInfo  // Pass shader info to be stored in pipeline
    );

    if (!outPipeline || !outPipeline->isValid())
    {
        spdlog::error("Failed to create pipeline '{}'", name);
        return false;
    }

    return true;
}

} // namespace engine::rendering