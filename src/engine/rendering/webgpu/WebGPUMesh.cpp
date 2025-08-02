#include "engine/rendering/webgpu/WebGPUMesh.h"

namespace engine::rendering::webgpu
{
	WebGPUMesh::WebGPUMesh(
		WebGPUContext &context,
		const engine::rendering::Mesh::Handle &meshHandle,
		wgpu::Buffer vertexBuffer,
		wgpu::Buffer indexBuffer,
		uint32_t vertexCount,
		uint32_t indexCount,
		WebGPUMeshOptions options)
		: WebGPURenderObject<engine::rendering::Mesh>(context, meshHandle, Type::Mesh),
		  m_vertexBuffer(vertexBuffer),
		  m_indexBuffer(indexBuffer),
		  m_vertexCount(vertexCount),
		  m_indexCount(indexCount),
		  m_options(std::move(options)) {}

	void WebGPUMesh::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
	{
		renderPass.setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount * sizeof(uint32_t));

		if (m_indexCount > 0)
		{
			renderPass.setIndexBuffer(m_indexBuffer, wgpu::IndexFormat::Uint32, 0, m_indexCount * sizeof(uint32_t));
			renderPass.drawIndexed(m_indexCount, 1, 0, 0, 0);
		}
		else
		{
			renderPass.draw(m_vertexCount, 1, 0, 0);
		}
	}

	void WebGPUMesh::updateGPUResources()
	{
		// This would be called when the CPU mesh changes
		// Here you would update the vertex and index buffers
		// For example:
		
		try {
			const auto& mesh = getCPUObject();
			
			// Example of what might go here:
			// 1. Check if vertex or index data has changed
			// 2. Update or recreate buffers as needed
			
			// In a real implementation, you might ask the factory to update the buffers:
			// auto [newVertexBuffer, newIndexBuffer] = 
			//     m_context.meshFactory().recreateBuffers(getCPUHandle(), m_options);
			// m_vertexBuffer = newVertexBuffer;
			// m_indexBuffer = newIndexBuffer;
			// m_vertexCount = mesh.vertices.size();
			// m_indexCount = mesh.indices.size();
		}
		catch (const std::exception& e) {
			// Log error or handle invalid mesh
		}
	}

} // namespace engine::rendering::webgpu
