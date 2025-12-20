#include "engine/rendering/webgpu/WebGPUMaterial.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupLayout.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu
{

WebGPUMaterial::WebGPUMaterial(
	WebGPUContext &context,
	const engine::rendering::Material::Handle &materialHandle,
	std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>> textures,
	WebGPUMaterialOptions options
) :
	WebGPURenderObject<engine::rendering::Material>(context, materialHandle, Type::Material),
	m_textures(std::move(textures)),
	m_options(std::move(options))
{
}

void WebGPUMaterial::render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass)
{
	const auto &cpuMaterial = getCPUObject();

	auto shaderInfo = m_context.shaderRegistry().getShader(m_shaderType, m_customShaderName);
	if (!shaderInfo)
	{
		spdlog::warn("WebGPUMaterial: Could not find shader (type={}, customName='{}')", static_cast<int>(m_shaderType), m_customShaderName);
		return;
	}

	// Get bind group 3 (material bind group)
	const auto &bindGroups = shaderInfo->getBindGroups();
	if (bindGroups.size() <= 3)
	{
		spdlog::warn("WebGPUMaterial: Shader has no bind group 3 for materials");
		return;
	}

	auto materialBindGroup = bindGroups[3];
	if (!materialBindGroup)
	{
		spdlog::warn("WebGPUMaterial: Bind group 3 is null");
		return;
	}

	auto rawBindGroup = materialBindGroup->getBindGroup();
	if (!rawBindGroup)
	{
		spdlog::warn("WebGPUMaterial: Material bind group not created yet - call updateGPUResources() first");
		return;
	}

	// CRITICAL: Bind the material bind group to slot 3
	renderPass.setBindGroup(3, rawBindGroup, 0, nullptr);
}

void WebGPUMaterial::updateGPUResources()
{
	const auto &cpuMaterial = getCPUObject();

	// Determine shader type and name
	ShaderType shaderType = cpuMaterial.getShaderType();
	std::string customShaderName = (shaderType == ShaderType::Custom) ? cpuMaterial.getCustomShaderName() : "";

	bool shaderChanged = false;

	// Check if the shader actually changed
	if (shaderType != m_shaderType)
	{
		shaderChanged = true;
		m_shaderType = shaderType;
	}
	if (customShaderName != m_customShaderName)
	{
		shaderChanged = true;
		m_customShaderName = customShaderName;
	}

	// ToDo: If shader changed recreate whole otherwise maybe just properties

	// Retrieve shader info
	std::shared_ptr<WebGPUShaderInfo> shaderInfo;
	if (shaderType == ShaderType::Custom)
	{
		shaderInfo = m_context.shaderRegistry().getCustomShader(customShaderName);
	}
	else
	{
		shaderInfo = m_context.shaderRegistry().getShader(shaderType);
	}

	if (!shaderInfo)
	{
		spdlog::warn("WebGPUMaterial: Shader not found (type={}, customName='{}')", static_cast<int>(shaderType), customShaderName);
		return;
	}

	// Get material bind group (bind group 3)
	const auto &bindGroups = shaderInfo->getBindGroups();
	if (bindGroups.size() <= 3 || !bindGroups[3])
	{
		spdlog::warn("WebGPUMaterial: Shader has no bind group 3 for material");
		return;
	}

	auto materialBindGroup = bindGroups[3];
	auto layoutInfo = materialBindGroup->getLayoutInfo();
	if (!layoutInfo)
	{
		spdlog::warn("WebGPUMaterial: Bind group 3 has no layout info");
		return;
	}

	std::vector<wgpu::BindGroupEntry> entries;
	entries.reserve(layoutInfo->getEntries().size());
	bool allResourcesReady = true;

	for (const auto &layoutEntry : layoutInfo->getEntries())
	{
		wgpu::BindGroupEntry entry{};
		entry.binding = layoutEntry.binding;

		if (layoutEntry.buffer.type != wgpu::BufferBindingType::Undefined)
		{
			auto buffer = materialBindGroup->getBuffer(layoutEntry.binding);
			if (!buffer)
			{
				spdlog::warn("WebGPUMaterial: Buffer not found for binding {}", layoutEntry.binding);
				continue;
			}
			entry.buffer = buffer->getBuffer();
			entry.offset = 0;
			entry.size = layoutEntry.buffer.minBindingSize;
		}
		else if (layoutEntry.texture.sampleType != wgpu::TextureSampleType::Undefined)
		{
			std::string slotName = layoutInfo->getMaterialSlotName(layoutEntry.binding);
			if (slotName.empty())
			{
				spdlog::warn("WebGPUMaterial: No slot name for texture binding {}", layoutEntry.binding);
				continue;
			}

			auto texture = getTexture(slotName);
			if (!texture || !texture->getTextureView())
			{
				spdlog::debug("WebGPUMaterial: Texture '{}' not ready for binding {}", slotName, layoutEntry.binding);
				allResourcesReady = false;
				break;
			}

			entry.textureView = texture->getTextureView();
		}
		else if (layoutEntry.sampler.type != wgpu::SamplerBindingType::Undefined)
		{
			entry.sampler = m_context.getDefaultSampler();
		}

		entries.push_back(entry);
	}

	if (!allResourcesReady)
	{
		spdlog::debug("WebGPUMaterial: Not all resources ready, deferring bind group creation");
		return;
	}

	// Create or update the GPU bind group
	wgpu::BindGroup rawBindGroup = m_context.bindGroupFactory().createBindGroup(
		layoutInfo->getLayout(),
		entries
	);

	if (!rawBindGroup)
	{
		spdlog::error("WebGPUMaterial: Failed to create bind group 3");
		return;
	}

	materialBindGroup->setBindGroup(rawBindGroup);

	// Update material properties buffer
	shaderInfo->updateBindGroupBuffer<Material::MaterialProperties>(
		3, // Bind group index
		0, // Binding index for material properties
		cpuMaterial.getProperties()
	);

	spdlog::debug("WebGPUMaterial: Bind group 3 updated for shader (type={}, customName='{}')", static_cast<int>(shaderType), customShaderName);
}

} // namespace engine::rendering::webgpu
