#include "engine/rendering/webgpu/WebGPUModel.h"

namespace engine::rendering::webgpu {

WebGPUModel::WebGPUModel(const engine::rendering::Model& model, std::shared_ptr<WebGPUMesh> mesh, std::shared_ptr<WebGPUMaterial> material) {
    // TODO: Store references
}

std::shared_ptr<WebGPUMesh> WebGPUModel::getMesh() const { /* ... */ return nullptr; }
std::shared_ptr<WebGPUMaterial> WebGPUModel::getMaterial() const { /* ... */ return nullptr; }

} // namespace engine::rendering::webgpu
