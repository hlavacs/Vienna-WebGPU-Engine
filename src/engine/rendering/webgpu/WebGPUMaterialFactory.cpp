#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/material/Material.h"
#include <memory>

WebGPUMaterialFactory::WebGPUMaterialFactory(WebGPUContext& context)
    : m_context(context) {}

std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFrom(const Material& material) {
    // TODO: Implement actual GPU upload logic
    static WebGPUTexture dummyBaseColor(m_context, engine::rendering::Texture());
    static WebGPUTexture dummyNormalMap(m_context, engine::rendering::Texture());
    return std::make_shared<WebGPUMaterial>(m_context, material, dummyBaseColor, dummyNormalMap);
}
