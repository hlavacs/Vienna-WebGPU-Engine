#include "engine/rendering/webgpu/WebGPUMesh.h"

namespace engine::rendering::webgpu
{

	WebGPUMesh::WebGPUMesh(wgpu::Buffer vertexBuffer, uint32_t vertexCount, wgpu::Buffer indexBuffer, uint32_t indexCount)
		: m_vertexBuffer(vertexBuffer), m_vertexCount(vertexCount), m_indexBuffer(indexBuffer), m_indexCount(indexCount)
	{
	}

	wgpu::Buffer WebGPUMesh::getVertexBuffer() const { return m_vertexBuffer; }
	wgpu::Buffer WebGPUMesh::getIndexBuffer() const { return m_indexBuffer; }
	uint32_t WebGPUMesh::getIndexCount() const { return m_indexCount; }
	uint32_t WebGPUMesh::getVertexCount() const { return m_vertexCount; }

} // namespace engine::rendering::webgpu
