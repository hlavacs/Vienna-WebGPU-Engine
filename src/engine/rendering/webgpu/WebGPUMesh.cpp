#include "engine/rendering/webgpu/WebGPUMesh.h"

namespace engine::rendering::webgpu {

WebGPUMesh::WebGPUMesh(WebGPUContext& context, const engine::rendering::Mesh& mesh)
    : m_context(context)
{
    // TODO: Upload mesh data to GPU buffers using m_context
}

wgpu::Buffer WebGPUMesh::getVertexBuffer() const { /* ... */ return {}; }
wgpu::Buffer WebGPUMesh::getIndexBuffer() const { /* ... */ return {}; }
uint32_t WebGPUMesh::getIndexCount() const { /* ... */ return 0; }
uint32_t WebGPUMesh::getVertexCount() const { /* ... */ return 0; }

} // namespace engine::rendering::webgpu
