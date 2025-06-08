#include "engine/rendering/webgpu/WebGPUMeshFactory.h"

namespace engine::rendering::webgpu {

WebGPUMeshFactory::WebGPUMeshFactory(WebGPUContext& context)
    : m_context(context) {}

std::shared_ptr<WebGPUMesh> WebGPUMeshFactory::createFrom(const engine::rendering::Mesh& mesh) {
    return std::make_shared<WebGPUMesh>(m_context, mesh);
}

} // namespace engine::rendering::webgpu
