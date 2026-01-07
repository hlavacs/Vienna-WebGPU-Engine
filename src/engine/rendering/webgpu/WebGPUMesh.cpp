#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/Vertex.h"

namespace engine::rendering::webgpu
{

void WebGPUMesh::bindBuffers(wgpu::RenderPassEncoder &renderPass, VertexLayout layout) const
{
	size_t vertexStride = Vertex::getStride(layout);
	renderPass.setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount * vertexStride);

	if (isIndexed())
	{
		renderPass.setIndexBuffer(m_indexBuffer, wgpu::IndexFormat::Uint32, 0, m_indexCount * sizeof(uint32_t));
	}
}

void WebGPUMesh::syncFromCPU(const Mesh &cpuMesh)
{
	// This would update vertex/index buffers if mesh data changed
	// For now, meshes are typically immutable after creation
	// But if you want to support dynamic meshes, implement buffer updates here
}

} // namespace engine::rendering::webgpu
