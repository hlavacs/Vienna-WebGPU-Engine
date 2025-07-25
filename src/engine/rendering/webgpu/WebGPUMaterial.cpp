#include "engine/rendering/webgpu/WebGPUMaterial.h"

namespace engine::rendering::webgpu
{

	WebGPUMaterial::WebGPUMaterial(
		WebGPUContext &context,
		const engine::rendering::Material::Handle &materialHandle,
		wgpu::BindGroup bindGroup,
		WebGPUMaterialOptions options)
		: WebGPURenderObject(context, Type::Material),
		  m_materialHandle(materialHandle),
		  m_bindGroup(bindGroup),
		  m_options(std::move(options)) {}

	wgpu::BindGroup WebGPUMaterial::getBindGroup() const { return m_bindGroup; }

	const engine::rendering::Material &WebGPUMaterial::getMaterial() const {
		return *m_materialHandle.get().value();
	}

	const WebGPUMaterialOptions &WebGPUMaterial::getOptions() const { return m_options; }

	void WebGPUMaterial::setOptions(const WebGPUMaterialOptions &opts) { m_options = opts; }

} // namespace engine::rendering::webgpu
