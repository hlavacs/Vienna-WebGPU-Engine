#include "engine/rendering/webgpu/WebGPUMesh.h"

namespace engine::rendering::webgpu
{

void WebGPUMesh::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
{
	renderPass.setVertexBuffer(0, m_vertexBuffer, 0, m_vertexCount * sizeof(uint32_t));

	if (isIndexed())
	{
		renderPass.setIndexBuffer(m_indexBuffer, wgpu::IndexFormat::Uint32, 0, m_indexCount * sizeof(uint32_t));
	}

	for (auto &sub : m_submeshes)
	{
		if (sub.material)
			sub.material->render(encoder, renderPass);

		if (isIndexed())
			renderPass.drawIndexed(sub.indexCount, 1, sub.indexOffset, 0, 0);
		else
			renderPass.draw(sub.indexCount, 1, sub.indexOffset, 0);
	}
}

void WebGPUMesh::updateGPUResources()
{
	try
	{
		const auto &mesh = getCPUObject();


		// Optionally update submeshes if CPU mesh exposes them (not shown here)
		// If submesh material pointers need to be updated, do so here
		for (auto &sub : m_submeshes) {
			if (sub.material) {
				sub.material->updateGPUResources();
			}
		}
	}
	catch (const std::exception &e)
	{
		// Log error or handle invalid mesh
	}
}

} // namespace engine::rendering::webgpu
