#pragma once
/**
 * @file WebGPUTexture.h
 * @brief GPU-side texture: wraps wgpu::Texture and wgpu::TextureView.
 */
#include <webgpu/webgpu.hpp>
#include "engine/rendering/Texture.h"

namespace engine::rendering::webgpu {

/**
 * @class WebGPUTexture
 * @brief Uploads Texture data to GPU and creates a view.
 */
class WebGPUTexture {
public:
    /**
     * @brief Construct from a CPU-side Texture.
     */
    WebGPUTexture(WebGPUContext& context, const engine::rendering::Texture& texture);
    /** @brief Get the GPU texture. */
    wgpu::Texture getTexture() const;
    /** @brief Get the texture view. */
    wgpu::TextureView getTextureView() const;
private:
    WebGPUContext& m_context;
    // ...internal GPU texture members...
};

} // namespace engine::rendering::webgpu
