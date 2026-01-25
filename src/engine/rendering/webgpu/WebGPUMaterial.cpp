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
) : WebGPUSyncObject<engine::rendering::Material>(context, materialHandle),
	m_textures(std::move(textures)),
	m_options(std::move(options))
{
	// Cache initial texture versions
	const auto &cpuMaterial = getCPUObject();
	cacheTextureVersions(cpuMaterial);
}

bool WebGPUMaterial::needsSync(const Material &cpuMaterial) const
{
	// Check material version
	if (cpuMaterial.getVersion() > m_lastSyncedVersion)
		return true;

	// Check if any texture versions changed
	for (const auto &[slotName, textureSlot] : cpuMaterial.getTextureSlots())
	{
		if (!textureSlot.handle.valid())
			continue;

		auto texOpt = textureSlot.handle.get();
		if (!texOpt.has_value())
			continue;

		auto &tex = texOpt.value();

		// Check cached texture version
		auto it = m_textureVersions.find(slotName);
		if (it == m_textureVersions.end() || it->second < tex->getVersion())
			return true;
	}

	return false;
}

void WebGPUMaterial::syncFromCPU(const Material &cpuMaterial)
{
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

	auto layout = shaderInfo->getBindGroupLayout(bindgroup::defaults::MATERIAL); // ToDo: Handle Custom Material Bind Groups
	if (!layout)
	{
		spdlog::warn("WebGPUMaterial: Shader has no bind group layout {} for material", bindgroup::defaults::MATERIAL);
		return;
	}

	if (!m_materialBindGroup || layout != m_materialBindGroup->getLayoutInfo())
	{
		m_materialBindGroup = m_context.bindGroupFactory().createBindGroup(layout, {}, shared_from_this());
	}

	auto materialBindGroupBindingIndex = layout->getBindingIndex(bindgroup::entry::defaults::MATERIAL_PROPERTIES);
	if(!materialBindGroupBindingIndex.has_value())
	{
		spdlog::warn("WebGPUMaterial: Material bind group layout missing MATERIAL binding");
		return;
	}

	// Update material properties buffer
	m_materialBindGroup->updateBuffer(
		materialBindGroupBindingIndex.value(),
		reinterpret_cast<const uint8_t *>(cpuMaterial.getPropertiesData()),
		cpuMaterial.getPropertiesSize(),
		0,
		m_context.getQueue()
	);

	// Update cached texture versions
	cacheTextureVersions(cpuMaterial);
}

void WebGPUMaterial::cacheTextureVersions(const Material &cpuMaterial)
{
	m_textureVersions.clear();
	for (const auto &[slotName, textureSlot] : cpuMaterial.getTextureSlots())
	{
		if (!textureSlot.handle.valid())
			continue;
		auto texOpt = textureSlot.handle.get();
		if (!texOpt.has_value())
			continue;
		m_textureVersions[slotName] = texOpt.value()->getVersion();
	}
}

} // namespace engine::rendering::webgpu
