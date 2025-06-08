#pragma once
/**
 * @file WebGPUMaterial.h
 * @brief GPU-side material: wraps bind groups and layout setup.
 */
#include <webgpu/webgpu.hpp>
#include "engine/rendering/Material.h"

namespace engine::rendering::webgpu {

class WebGPUTexture;

/**
 * @class WebGPUMaterial
 * @brief Creates bind groups for a Material, referencing WebGPUTexture.
 */
class WebGPUMaterial {
public:
    /**
     * @brief Construct from a CPU-side Material and GPU textures.
     */
    WebGPUMaterial(WebGPUContext& context, const engine::rendering::Material& material, const WebGPUTexture& baseColor, const WebGPUTexture& normalMap);
    /** @brief Get the bind group. */
    wgpu::BindGroup getBindGroup() const;
private:
    WebGPUContext& m_context;
    // ...internal bind group members...
};

} // namespace engine::rendering::webgpu
