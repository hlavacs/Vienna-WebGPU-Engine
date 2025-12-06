#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"

namespace engine::rendering
{

/**
 * @brief Manages render passes and their configurations.
 * 
 * Handles creation, caching, and lifecycle of render passes.
 * Supports multiple render passes with type-safe ID-based access.
 */
class RenderPassManager
{
public:
    RenderPassManager(webgpu::WebGPUContext &context);
    ~RenderPassManager();

    /**
     * @brief Registers a render pass configuration.
     * @param passContext The render pass context configuration.
     */
    void registerPass(std::shared_ptr<webgpu::WebGPURenderPassContext> passContext);

    /**
     * @brief Begins a render pass by ID.
     * @param passId The render pass identifier.
     * @param encoder Command encoder to use.
     * @return The active render pass encoder.
     */
    wgpu::RenderPassEncoder beginPass(uint64_t passId, wgpu::CommandEncoder encoder);

    /**
     * @brief Gets a render pass context by ID.
     * @param passId The render pass identifier.
     * @return Shared pointer to the render pass context.
     */
    std::shared_ptr<webgpu::WebGPURenderPassContext> getPassContext(uint64_t passId);


    // ToDo: xml doc
    /**
     * @brief Updates render pass attachments (e.g., on resize).
     * @param passId The render pass identifier.
     * @param surfaceTexture New surface texture view.
     * @param depthBuffer New depth buffer.
     */
    void updatePassAttachments(
        uint64_t passId,
	const std::shared_ptr<webgpu::WebGPUTexture> &colorTexture,
        std::shared_ptr<webgpu::WebGPUDepthTexture> depthBuffer
    );

    /**
     * @brief Removes a render pass.
     * @param passId The render pass identifier.
     */
    void removePass(uint64_t passId);

    /**
     * @brief Clears all render passes.
     */
    void clear();

private:
    webgpu::WebGPUContext &m_context;
    std::unordered_map<uint64_t, std::shared_ptr<webgpu::WebGPURenderPassContext>> m_passes;
};

} // namespace engine::rendering