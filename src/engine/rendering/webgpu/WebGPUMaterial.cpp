#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUShaderInfo.h"
#include <spdlog/spdlog.h>

namespace engine::rendering::webgpu
{

WebGPUMaterial::WebGPUMaterial(
	WebGPUContext &context,
	const engine::rendering::Material::Handle &materialHandle,
	std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>> textures,
	WebGPUMaterialOptions options
) : WebGPURenderObject<engine::rendering::Material>(context, materialHandle, Type::Material),
	m_textures(std::move(textures)),
	m_options(std::move(options))
{
}

void WebGPUMaterial::updateGPUResources()
{
	const auto &cpuMaterial = getCPUObject();

	// Determine shader type and custom shader
	const std::string &shaderName = cpuMaterial.getShader();
	bool shaderChanged = shaderName != m_shaderName;
	m_shaderName = shaderName;

	// Get shader info
	std::shared_ptr<WebGPUShaderInfo> shaderInfo =
		m_context.shaderRegistry().getShader(shaderName);

	if (!shaderInfo)
	{
		spdlog::warn("WebGPUMaterial: Shader not found (name='{}')", shaderName);
		return;
	}

	// Create or update material bind group (group 3)
	auto layout = shaderInfo->getBindGroupLayout(3);
	if (!layout)
	{
		spdlog::warn("WebGPUMaterial: Shader has no bind group layout 3 for material");
		return;
	}

	if (!m_materialBindGroup)
	{
		m_materialBindGroup = m_context.bindGroupFactory().createBindGroup(layout, shared_from_this());
	}

	m_materialBindGroup->updateBuffer(
		0, // binding 0 for material properties
		reinterpret_cast<const uint8_t *>(&cpuMaterial.getProperties()),
		cpuMaterial.getPropertiesSize(),
		0,
		m_context.getQueue()
	);
}

void WebGPUMaterial::bind(wgpu::RenderPassEncoder &pass) const
{
	if (!m_materialBindGroup || !m_materialBindGroup->getBindGroup())
		return;

	// Always bind at group 3
	pass.setBindGroup(3, m_materialBindGroup->getBindGroup(), 0, nullptr);
}

} // namespace engine::rendering::webgpu
