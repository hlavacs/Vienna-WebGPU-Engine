#include "engine/rendering/webgpu/WebGPUMaterial.h"

namespace engine::rendering::webgpu {

WebGPUMaterial::WebGPUMaterial(WebGPUContext& context, const engine::rendering::Material& material, const WebGPUTexture& baseColor, const WebGPUTexture& normalMap)
    : m_context(context)
{
    // TODO: Create bind group for material using m_context
}

wgpu::BindGroup WebGPUMaterial::getBindGroup() const { /* ... */ return {}; }

} // namespace engine::rendering::webgpu
