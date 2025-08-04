#include "engine/rendering/webgpu/WebGPUMaterial.h"

#include "engine/rendering/BindGroupLayout.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUMaterial::WebGPUMaterial(WebGPUContext &context, const engine::rendering::Material::Handle &materialHandle, wgpu::BindGroup bindGroup, wgpu::Buffer propertiesBuffer, WebGPUMaterialTextures textures, WebGPUMaterialOptions options) :
	WebGPURenderObject<engine::rendering::Material>(context, materialHandle, Type::Material),
	m_bindGroup(bindGroup),
	m_propertiesBuffer(propertiesBuffer),
	m_textures(textures),
	m_options(std::move(options))
{
}

void WebGPUMaterial::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
{
	renderPass.setBindGroup(BindGroupLayoutIndex::MaterialIndex, m_bindGroup, 0, nullptr);
}

void WebGPUMaterial::updateGPUResources()
{
	wgpuQueueWriteBuffer(
		m_context.getQueue(),
		m_propertiesBuffer,
		0,
		&getCPUObject().getProperties(),
		sizeof(engine::rendering::Material::MaterialProperties)
	);
}

} // namespace engine::rendering::webgpu
