#pragma once
#include <memory>
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"

class WebGPUContext;
class Material;
class WebGPUMaterial;

class WebGPUMaterialFactory : public engine::rendering::webgpu::BaseWebGPUFactory<Material, WebGPUMaterial> {
public:
    explicit WebGPUMaterialFactory(WebGPUContext& context);
    std::shared_ptr<WebGPUMaterial> createFrom(const Material& material) override;
private:
    WebGPUContext& m_context;
};
