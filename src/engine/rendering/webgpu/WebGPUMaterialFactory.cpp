#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"

#include <memory>
#include <optional>
#include <stdexcept>

#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu
{

WebGPUMaterialFactory::WebGPUMaterialFactory(WebGPUContext &context) :
	BaseWebGPUFactory(context)
{
}

static std::shared_ptr<WebGPUTexture> getTextureView(
	WebGPUContext &context,
	const TextureHandle &textureHandle,
	const std::optional<glm::vec3> &fallbackColor
)
{
	auto &texFactory = context.textureFactory();

	// First check if the texture handle is valid
	if (textureHandle.valid())
	{
		auto texOpt = textureHandle.get();
		if (texOpt && texOpt.value())
		{
			// Use the texture if it exists
			return texFactory.createFrom(*texOpt.value());
		}
	}

	// If no valid texture, use fallback color if provided
	if (fallbackColor.has_value())
	{
		const auto &color = fallbackColor.value();
		// Only create a texture from color if not default white
		if (color != glm::vec3(1.0f))
		{
			return texFactory.createFromColor(color, 1, 1);
		}
	}

	// Default to white texture
	return texFactory.getWhiteTexture();
}

std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFromHandle(
	const engine::rendering::Material::Handle &materialHandle,
	const WebGPUMaterialOptions &options
)
{
	auto materialOpt = materialHandle.get();
	if (!materialOpt || !materialOpt.value())
	{
		throw std::runtime_error("Invalid material handle in WebGPUMaterialFactory::createFromHandle");
	}

	const auto &material = *materialOpt.value();

	// Get the material properties
	const auto &materialProps = material.getProperties();

	// Create diffuse color from properties
	glm::vec3 diffuseColor(materialProps.diffuse[0], materialProps.diffuse[1], materialProps.diffuse[2]);

	// Get texture views using the helper method
	std::shared_ptr<WebGPUTexture> albedoView = getTextureView(
		m_context,
		material.getAlbedoTexture(),
		std::optional<glm::vec3>(diffuseColor)
	);

	std::shared_ptr<WebGPUTexture> normalView = getTextureView(
		m_context,
		material.getNormalTexture(),
		std::nullopt // No color fallback for normal maps
	);

	wgpu::Buffer materialPropertiesBuffer =
		m_context.bufferFactory().createUniformBuffer<Material::MaterialProperties>(&materialProps, 1u);

	// Get additional texture views
	std::shared_ptr<WebGPUTexture> metallicView = getTextureView(
		m_context,
		material.getMetallicTexture(),
		std::nullopt
	);
	std::shared_ptr<WebGPUTexture> roughnessView = getTextureView(
		m_context,
		material.getRoughnessTexture(),
		std::nullopt
	);
	std::shared_ptr<WebGPUTexture> aoView = getTextureView(
		m_context,
		material.getAOTexture(),
		std::nullopt
	);
	std::shared_ptr<WebGPUTexture> emissiveView = getTextureView(
		m_context,
		material.getEmissiveTexture(),
		std::optional<glm::vec3>(glm::vec3(materialProps.emission[0], materialProps.emission[1], materialProps.emission[2]))
	);

	wgpu::Sampler sampler = m_context.getDefaultSampler();
	if (m_bindGroupLayoutInfo == nullptr)
	{
		m_bindGroupLayoutInfo = m_context.bindGroupFactory().createDefaultMaterialBindGroupLayout();
	}

	wgpu::BindGroup bindGroup = m_context.bindGroupFactory().createMaterialBindGroup(
		m_bindGroupLayoutInfo->getLayout(),
		materialPropertiesBuffer,
		albedoView->getTextureView(),
		normalView->getTextureView(),
		sampler
	);

	// Construct the texture dictionary with all loaded textures
	std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>> textureMap;
	textureMap[MaterialTextureSlots::ALBEDO] = albedoView;
	textureMap[MaterialTextureSlots::NORMAL] = normalView;
	textureMap[MaterialTextureSlots::METALLIC] = metallicView;
	textureMap[MaterialTextureSlots::ROUGHNESS] = roughnessView;
	textureMap[MaterialTextureSlots::AO] = aoView;
	textureMap[MaterialTextureSlots::EMISSIVE] = emissiveView;

	auto webgpuMaterial = std::make_shared<WebGPUMaterial>(
		m_context,
		materialHandle,
		textureMap,
		options
	);

	// Determine pipeline name based on material's shader type
	std::string pipelineName = "main"; // Default to main (Lit shader)
	ShaderType shaderType = material.getShaderType();
	
	switch (shaderType)
	{
		case ShaderType::Lit:
			pipelineName = "main";
			break;
		case ShaderType::Debug:
			pipelineName = "debug";
			break;
		case ShaderType::Unlit:
			// Future: when unlit shader is implemented
			pipelineName = "unlit";
			break;
		case ShaderType::Custom:
			// Future: custom shader pipeline lookup
			pipelineName = "main"; // Fallback to main for now
			break;
	}
	
	webgpuMaterial->setPipelineName(pipelineName);

	// NOTE: Pipeline handle must be set externally before calling updateGPUResources()
	// This is for backwards compatibility - prefer using createFromHandle with pipeline handle

	return webgpuMaterial;
}

std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFromHandle(
	const engine::rendering::Material::Handle &handle
)
{
	return createFromHandle(handle, WebGPUMaterialOptions{});
}

} // namespace engine::rendering::webgpu
