#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"

namespace engine::rendering::webgpu {

WebGPUModel::WebGPUModel(std::shared_ptr<WebGPUMesh> mesh, std::shared_ptr<WebGPUMaterial> material)
	: m_mesh(std::move(mesh)), m_material(std::move(material)) {}

std::shared_ptr<WebGPUMesh> WebGPUModel::getMesh() const { return m_mesh; }
std::shared_ptr<WebGPUMaterial> WebGPUModel::getMaterial() const { return m_material; }

} // namespace engine::rendering::webgpu
