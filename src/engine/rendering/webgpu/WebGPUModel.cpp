#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"

namespace engine::rendering::webgpu
{

WebGPUModel::WebGPUModel(
	WebGPUContext &context,
	const engine::rendering::Model::Handle &modelHandle,
	std::shared_ptr<WebGPUMesh> mesh,
	std::shared_ptr<WebGPUMaterial> material,
	WebGPUModelOptions options
) :
	WebGPURenderObject<engine::rendering::Model>(context, modelHandle, Type::Model),
	m_mesh(std::move(mesh)),
	m_material(std::move(material)),
	m_options(std::move(options)) {}

void WebGPUModel::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
{
	// 1. Bind material resources first (bind group 3)
	if (m_material)
	{
		m_material->render(encoder, renderPass);
	}

	// 2. Then render the mesh with the material bound
	if (m_mesh)
	{
		m_mesh->render(encoder, renderPass);
	}
}

void WebGPUModel::updateGPUResources()
{
	// Update mesh if it has changed
	if (m_mesh)
	{
		m_mesh->update();
	}

	// Update material if it has changed
	// This will trigger WebGPUMaterial::updateGPUResources() only if dirty
	if (m_material)
	{
		m_material->update();
	}
}

} // namespace engine::rendering::webgpu
