#include "engine/rendering/webgpu/WebGPUMeshFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include <memory>
#include <stdexcept>

namespace engine::rendering::webgpu
{

WebGPUMeshFactory::WebGPUMeshFactory(WebGPUContext &context) :
	BaseWebGPUFactory(context) {}

std::shared_ptr<WebGPUMesh> WebGPUMeshFactory::createFromHandleUncached(
	const engine::rendering::Mesh::Handle &handle
)
{
	auto meshOpt = handle.get();
	if (!meshOpt || !meshOpt.value())
	{
		throw std::runtime_error("Invalid mesh handle in WebGPUMeshFactory::createFromHandleUncached");
	}
	const auto &mesh = *meshOpt.value();

	return std::make_shared<WebGPUMesh>(
		m_context,
		handle,
		static_cast<uint32_t>(mesh.getVertices().size()),
		mesh.isIndexed() ? static_cast<uint32_t>(mesh.getIndices().size()) : 0
	);
}
} // namespace engine::rendering::webgpu
