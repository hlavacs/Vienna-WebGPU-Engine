#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu {

WebGPUTexture::WebGPUTexture(WebGPUContext& context, const engine::rendering::Texture& texture)
    : m_context(context)
{
    // TODO: Upload texture data to GPU and create view using m_context
}

wgpu::Texture WebGPUTexture::getTexture() const { /* ... */ return {}; }
wgpu::TextureView WebGPUTexture::getTextureView() const { /* ... */ return {}; }

} // namespace engine::rendering::webgpu
