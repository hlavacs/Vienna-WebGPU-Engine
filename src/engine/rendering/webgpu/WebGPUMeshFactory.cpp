#include "engine/rendering/webgpu/WebGPUMeshFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/Mesh.h"

namespace engine::rendering::webgpu
{

	WebGPUMeshFactory::WebGPUMeshFactory(WebGPUContext &context)
		: BaseWebGPUFactory(context) {}

	std::shared_ptr<WebGPUMesh> WebGPUMeshFactory::createFrom(const engine::rendering::Mesh &mesh)
	{
		// Upload vertex buffer
		wgpu::Buffer vertexBuffer = m_context.createBufferWithData(mesh.vertices, wgpu::BufferUsage::Vertex);
		uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
		// Upload index buffer if present
		wgpu::Buffer indexBuffer = nullptr;
		uint32_t indexCount = 0;
		if (mesh.isIndexed() && !mesh.indices.empty()) {
			indexBuffer = m_context.createBufferWithData(mesh.indices, wgpu::BufferUsage::Index);
			indexCount = static_cast<uint32_t>(mesh.indices.size());
		}
		return std::make_shared<WebGPUMesh>(vertexBuffer, vertexCount, indexBuffer, indexCount);
	}

} // namespace engine::rendering::webgpu
