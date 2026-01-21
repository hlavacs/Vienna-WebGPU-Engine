#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/Vertex.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

const WebGPUMesh::VertexBufferEntry &WebGPUMesh::ensureBufferForLayout(VertexLayout layout) const
{
	auto it = m_vertexBuffers.find(layout);
	if (it == m_vertexBuffers.end())
	{
		auto cpuMesh = getCPUHandle().get();
		if (!cpuMesh || !cpuMesh.value())
		{
			throw std::runtime_error("Invalid CPU mesh handle");
		}
		wgpu::Buffer buffer = nullptr;
		auto vertices = cpuMesh.value()->getVertices();
		auto packed = Vertex::repackVertices(vertices, layout);

		// Upload GPU buffer
		buffer = m_context.bufferFactory().createBufferWithData(
			packed,
			wgpu::BufferUsage::Vertex
		);
		VertexBufferEntry entry{buffer, static_cast<uint32_t>(vertices.size())};
		m_vertexBuffers[layout] = entry;
		it = m_vertexBuffers.find(layout);
	}
	return it->second;
}

void WebGPUMesh::bindBuffers(wgpu::RenderPassEncoder &renderPass, VertexLayout layout) const
{
	const auto &entry = ensureBufferForLayout(layout);
	renderPass.setVertexBuffer(0, entry.buffer, 0, entry.count * Vertex::getStride(layout));

	if (isIndexed())
		renderPass.setIndexBuffer(m_indexBuffer, wgpu::IndexFormat::Uint32, 0, m_indexCount * sizeof(uint32_t));
}

void WebGPUMesh::syncFromCPU(const Mesh &cpuMesh)
{
	wgpu::Buffer indexBuffer = nullptr;
	if (cpuMesh.isIndexed())
	{
		indexBuffer = m_context.bufferFactory().createBufferWithData(cpuMesh.getIndices(), wgpu::BufferUsage::Index);
	}
	m_indexBuffer = indexBuffer;
}

} // namespace engine::rendering::webgpu
