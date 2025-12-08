#include "engine/rendering/webgpu/WebGPUMaterial.h"

#include <spdlog/spdlog.h>

#include "engine/rendering/BindGroupLayout.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUPipeline.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/ShaderRegistry.h"

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
	
	// Validate pipeline handle
	if (!m_pipelineHandle.valid())
	{
		spdlog::warn("WebGPUMaterial: No pipeline handle set");
		return;
	}

	auto pipelineOpt = m_pipelineHandle.get();
	if (!pipelineOpt || !*pipelineOpt)
	{
		spdlog::warn("WebGPUMaterial: Invalid pipeline handle");
		return;
	}
	auto pipeline = *pipelineOpt;

	// Get shader info from pipeline
	auto shaderInfo = pipeline->getShaderInfo();
	if (!shaderInfo)
	{
		spdlog::warn("WebGPUMaterial: Pipeline has no shader info");
		return;
	}

	// Update material properties buffer
	shaderInfo->updateBindGroupBuffer<Material::MaterialProperties>(
		3, // Bind group index
		0, // Binding index for material properties
		cpuMaterial.getProperties()
	);

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

	// Get the raw bind group that was created in updateGPUResources()
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

	// Get the pipeline from handle
	if (!m_pipelineHandle.valid())
	{
		spdlog::warn("WebGPUMaterial: No pipeline handle set");
		return;
	}

	auto pipelineOpt = m_pipelineHandle.get();
	if (!pipelineOpt || !*pipelineOpt)
	{
		spdlog::warn("WebGPUMaterial: Invalid pipeline handle");
		return;
	}

	auto pipeline = *pipelineOpt;

	// Get shader info from pipeline
	auto shaderInfo = pipeline->getShaderInfo();
	if (!shaderInfo)
	{
		spdlog::warn("WebGPUMaterial: Pipeline has no shader info");
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

	auto layoutInfo = materialBindGroup->getLayoutInfo();
	if (!layoutInfo)
	{
		spdlog::warn("WebGPUMaterial: Bind group 3 has no layout info");
		return;
	}
	
	std::vector<wgpu::BindGroupEntry> entries;
	entries.reserve(layoutInfo->getEntries().size());
	
	bool allRequiredTexturesReady = true;

	for (const auto &layoutEntry : layoutInfo->getEntries())
	{
		wgpu::BindGroupEntry entry{};
		entry.binding = layoutEntry.binding;

		if (layoutEntry.buffer.type != wgpu::BufferBindingType::Undefined)
		{
			// Buffer binding - get the existing buffer from the bind group
			auto buffer = materialBindGroup->getBuffer(layoutEntry.binding);
			if (!buffer)
			{
				spdlog::warn("WebGPUMaterial: Buffer for binding {} not found in bind group", layoutEntry.binding);
				continue;
			}
			entry.buffer = buffer->getBuffer();
			entry.offset = 0;
			entry.size = layoutEntry.buffer.minBindingSize;
		}
		else if (layoutEntry.texture.sampleType != wgpu::TextureSampleType::Undefined)
		{
			// Texture binding - get material slot name from layout metadata
			std::string slotName = layoutInfo->getMaterialSlotName(layoutEntry.binding);
			
			if (slotName.empty())
			{
				spdlog::warn("WebGPUMaterial: No material slot name found for texture binding {}", layoutEntry.binding);
				continue;
			}

			// Get texture from material's dictionary using the slot name
			auto texture = getTexture(slotName);
			
			// Check if this REQUIRED texture is ready
			if (!texture || !texture->getTextureView())
			{
				spdlog::warn("WebGPUMaterial: Required texture '{}' for binding {} not ready", slotName, layoutEntry.binding);
				allRequiredTexturesReady = false;
				break; // Early exit - can't create bind group yet
			}

			entry.textureView = texture->getTextureView();
		}
		else if (layoutEntry.sampler.type != wgpu::SamplerBindingType::Undefined)
		{
			// Sampler binding - use default sampler (reusable)
			entry.sampler = m_context.getDefaultSampler();
		}
		entries.push_back(entry);
	}

	// Lazy creation - only create bind group if all required resources are ready
	if (!allRequiredTexturesReady)
	{
		spdlog::debug("WebGPUMaterial: Not all required textures ready, deferring bind group creation");
		return;
	}

	// Create the complete bind group with all resources
	wgpu::BindGroup rawBindGroup = m_context.bindGroupFactory().createBindGroup(
		layoutInfo->getLayout(),
		entries
	);

	if (!rawBindGroup)
	{
		spdlog::error("WebGPUMaterial: Failed to create material bind group");
		return;
	}

	// Update the shader's bind group 3 with the newly created bind group
	// Note: This replaces any existing bind group, which is necessary when textures change
	materialBindGroup->setBindGroup(rawBindGroup);

	spdlog::debug("WebGPUMaterial: Lazy-initialized bind group 3 with {} entries (shader's buffer reused, textures bound)",
		entries.size());
}

} // namespace engine::rendering::webgpu
