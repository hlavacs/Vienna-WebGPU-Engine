#include "engine/rendering/webgpu/WebGPUMeshFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include <memory>
#include <stdexcept>

namespace engine::rendering::webgpu
{

WebGPUMeshFactory::WebGPUMeshFactory(WebGPUContext &context) :
	BaseWebGPUFactory(context) {}

std::shared_ptr<WebGPUMesh> WebGPUMeshFactory::createFromHandle(
	const engine::rendering::Mesh::Handle &handle
)
{
	auto meshOpt = handle.get();
	if (!meshOpt || !meshOpt.value())
	{
		throw std::runtime_error("Invalid mesh handle in WebGPUMeshFactory::createFromHandle");
	}
	const auto &mesh = *meshOpt.value();

	// Upload vertex buffer
	wgpu::Buffer vertexBuffer = m_context.createBufferWithData(mesh.getVertices(), wgpu::BufferUsage::Vertex);
	uint32_t vertexCount = static_cast<uint32_t>(mesh.getVertices().size());

	// Upload index buffer if present
	wgpu::Buffer indexBuffer = nullptr;
	if (mesh.isIndexed() && !mesh.getIndices().empty())
	{
		indexBuffer = m_context.createBufferWithData(mesh.getIndices(), wgpu::BufferUsage::Index);
	}

	return std::make_shared<WebGPUMesh>(
		m_context,
		handle,
		vertexBuffer,
		indexBuffer,
		static_cast<uint32_t>(mesh.getVertices().size()),
		mesh.isIndexed() ? static_cast<uint32_t>(mesh.getIndices().size()) : 0
	);
}
} // namespace engine::rendering::webgpu
