#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/BindGroupLayout.h"
#include "engine/rendering/ShaderRegistry.h"
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
	ShaderType shaderType = cpuMaterial.getShaderType();
	std::string customShaderName = (shaderType == ShaderType::Custom) ? cpuMaterial.getCustomShaderName() : "";

	bool shaderChanged = shaderType != m_shaderType || customShaderName != m_customShaderName;
	m_shaderType = shaderType;
	m_customShaderName = customShaderName;

	// Get shader info
	std::shared_ptr<WebGPUShaderInfo> shaderInfo =
		(shaderType == ShaderType::Custom)
			? m_context.shaderRegistry().getCustomShader(customShaderName)
			: m_context.shaderRegistry().getShader(shaderType);

	if (!shaderInfo)
	{
		spdlog::warn("WebGPUMaterial: Shader not found (type={}, name='{}')", static_cast<int>(shaderType), customShaderName);
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

	m_materialBindGroup->updateBuffer(0, // binding 0 for material properties
		reinterpret_cast<const uint8_t *>(&cpuMaterial.getProperties()),
		sizeof(Material::MaterialProperties),
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
