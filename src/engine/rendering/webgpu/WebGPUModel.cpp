#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"

namespace engine::rendering::webgpu
{

	WebGPUModel::WebGPUModel(
		WebGPUContext &context,
		const engine::rendering::Model::Handle &modelHandle,
		std::shared_ptr<WebGPUMesh> mesh,
		std::shared_ptr<WebGPUMaterial> material,
		WebGPUModelOptions options)
		: WebGPURenderObject<engine::rendering::Model>(context, modelHandle, Type::Model),
		  m_mesh(std::move(mesh)),
		  m_material(std::move(material)),
		  m_options(std::move(options)) {}

	void WebGPUModel::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
	{
		// First set up the material state
		if (m_material)
		{
			m_material->render(encoder, renderPass);
		}

		// Then render the mesh
		if (m_mesh)
		{
			m_mesh->render(encoder, renderPass);
		}
	}

	void WebGPUModel::updateGPUResources()
	{
		try
		{
			// This would be called when the CPU model changes
			// Here you would update the mesh and material references
		}
		catch (const std::exception &e)
		{
			// Log error or handle invalid model
		}
	}

} // namespace engine::rendering::webgpu
