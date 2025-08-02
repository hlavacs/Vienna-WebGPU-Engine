#include "engine/rendering/webgpu/WebGPUMaterial.h"

namespace engine::rendering::webgpu
{

	WebGPUMaterial::WebGPUMaterial(
		WebGPUContext &context,
		const engine::rendering::Material::Handle &materialHandle,
		wgpu::BindGroup bindGroup,
		WebGPUMaterialOptions options)
		: WebGPURenderObject<engine::rendering::Material>(context, materialHandle, Type::Material),
		  m_bindGroup(bindGroup),
		  m_options(std::move(options)) {}

	void WebGPUMaterial::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
	{
		renderPass.setBindGroup(1, m_bindGroup, 0, nullptr);
	}

	void WebGPUMaterial::updateGPUResources()
	{
		// This method is called when the CPU material has changed or dirty flag is set
		// Here you would update the bind group with current material properties
		// For example:
		//
		// 1. Get updated textures from texture factory based on material's texture handles
		// 2. Update uniform buffer with material properties (colors, factors, etc.)
		// 3. Create new bind group if needed
		//
		// For now, this is a placeholder - in a real implementation, you would
		// ask the material factory to recreate or update the bind group

		// Example (pseudo-code):
		// m_bindGroup = getContext().materialFactory().recreateBindGroup(getCPUHandle(), m_options);
	}

} // namespace engine::rendering::webgpu
