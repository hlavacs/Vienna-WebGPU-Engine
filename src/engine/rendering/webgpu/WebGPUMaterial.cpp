#include "engine/rendering/webgpu/WebGPUMaterial.h"

namespace engine::rendering::webgpu
{

	WebGPUMaterial::WebGPUMaterial(wgpu::BindGroup bindGroup, WebGPUMaterialOptions options)
		: m_bindGroup(bindGroup), m_options(std::move(options)) {}

	wgpu::BindGroup WebGPUMaterial::getBindGroup() const { return m_bindGroup; }
	const WebGPUMaterialOptions& WebGPUMaterial::getOptions() const { return m_options; }
	void WebGPUMaterial::setOptions(const WebGPUMaterialOptions& opts) { m_options = opts; }

} // namespace engine::rendering::webgpu
