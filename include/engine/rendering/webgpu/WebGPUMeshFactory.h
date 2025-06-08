#pragma once
#include <memory>
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"

namespace engine::rendering::webgpu {

class WebGPUMeshFactory : public BaseWebGPUFactory<engine::rendering::Mesh, WebGPUMesh> {
public:
    using BaseWebGPUFactory::BaseWebGPUFactory;
    std::shared_ptr<WebGPUMesh> createFrom(const engine::rendering::Mesh& mesh) override;
};

} // namespace engine::rendering::webgpu
