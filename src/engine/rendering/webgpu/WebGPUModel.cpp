#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"

namespace engine::rendering::webgpu
{

WebGPUModel::WebGPUModel(
	WebGPUContext &context,
	const engine::rendering::Model::Handle &modelHandle,
	std::shared_ptr<WebGPUMesh> mesh,
	WebGPUModelOptions options
) :
	WebGPUSyncObject<engine::rendering::Model>(context, modelHandle),
	m_mesh(std::move(mesh)),
	m_options(std::move(options)) {}

void WebGPUModel::syncFromCPU(const Model &cpuModel)
{
	// Models typically don't have mutable GPU data after creation
	// If you need to support dynamic models, implement updates here
}

} // namespace engine::rendering::webgpu
