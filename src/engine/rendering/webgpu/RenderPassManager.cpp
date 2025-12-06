#include "engine/rendering/RenderPassManager.h"

#include <spdlog/spdlog.h>

namespace engine::rendering
{

RenderPassManager::RenderPassManager(webgpu::WebGPUContext &context)
    : m_context(context)
{
}

RenderPassManager::~RenderPassManager()
{
    clear();
}

void RenderPassManager::registerPass(std::shared_ptr<webgpu::WebGPURenderPassContext> passContext)
{
    if (!passContext)
    {
        spdlog::error("Cannot register null render pass context");
        return;
    }

    const uint64_t passId = passContext->getId();
    if (m_passes.find(passId) != m_passes.end())
    {
        spdlog::warn("Render pass with ID {} already registered, replacing", passId);
    }
    m_passes[passId] = passContext;
    spdlog::info("Registered render pass with ID: {}", passId);
}

wgpu::RenderPassEncoder RenderPassManager::beginPass(uint64_t passId, wgpu::CommandEncoder encoder)
{
    auto it = m_passes.find(passId);
    if (it == m_passes.end())
    {
        spdlog::error("Render pass with ID {} not found", passId);
        return nullptr;
    }

    return encoder.beginRenderPass(it->second->getRenderPassDescriptor());
}

std::shared_ptr<webgpu::WebGPURenderPassContext> RenderPassManager::getPassContext(uint64_t passId)
{
    auto it = m_passes.find(passId);
    if (it == m_passes.end())
    {
        spdlog::error("Render pass with ID {} not found", passId);
        return nullptr;
    }
    return it->second;
}

void RenderPassManager::updatePassAttachments(
    uint64_t passId,
	const std::shared_ptr<webgpu::WebGPUTexture> &surfaceTexture,
    std::shared_ptr<webgpu::WebGPUDepthTexture> depthBuffer)
{
    auto it = m_passes.find(passId);
    if (it == m_passes.end())
    {
        spdlog::error("Render pass with ID {} not found for update", passId);
        return;
    }
    it->second->updateView(surfaceTexture, depthBuffer);
}

void RenderPassManager::removePass(uint64_t passId)
{
    auto it = m_passes.find(passId);
    if (it != m_passes.end())
    {
        m_passes.erase(it);
        spdlog::info("Removed render pass with ID: {}", passId);
    }
}

void RenderPassManager::clear()
{
    m_passes.clear();
}

} // namespace engine::rendering