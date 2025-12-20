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
	WebGPURenderObject<engine::rendering::Model>(context, modelHandle, Type::Model),
	m_mesh(std::move(mesh)),
	m_options(std::move(options)) {}

void WebGPUModel::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
{
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
}

} // namespace engine::rendering::webgpu
