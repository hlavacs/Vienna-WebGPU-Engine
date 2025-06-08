#include "engine/rendering/webgpu/WebGPUTextureFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/texture/Texture.h"
#include <memory>

namespace engine::rendering::webgpu {

WebGPUTextureFactory::WebGPUTextureFactory(WebGPUContext& context)
    : m_context(context) {}

std::shared_ptr<WebGPUTexture> WebGPUTextureFactory::createFrom(const engine::rendering::Texture& texture) {
    // TODO: Implement actual GPU upload logic
    return std::make_shared<WebGPUTexture>(m_context, texture);
}

} // namespace engine::rendering::webgpu
