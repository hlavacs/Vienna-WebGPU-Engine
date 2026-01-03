#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"

#include <memory>
#include <optional>
#include <stdexcept>

#include "engine/rendering/ColorSpace.h"
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
	const std::optional<glm::vec3> &fallbackColor,
	ColorSpace colorSpace = ColorSpace::sRGB
)
{
	auto &texFactory = context.textureFactory();

	// First check if the texture handle is valid
	if (textureHandle.valid())
	{
		auto texOpt = textureHandle.get();
		if (texOpt && texOpt.value())
		{
			// Use the texture with the specified color space
			WebGPUTextureOptions options{};
			options.colorSpace = colorSpace;
			return texFactory.createFromHandle(textureHandle, options);
		}
	}

	// If no valid texture, use fallback color if provided
	if (fallbackColor.has_value())
	{
		const auto &color = fallbackColor.value();
		// Only create a texture from color if not default white
		if (color != glm::vec3(1.0f))
		{
			return texFactory.createFromColor(color, 1, 1, colorSpace);
		}
	}

	// Default to white texture (always sRGB for fallback)
	return texFactory.getWhiteTexture();
}

std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFromHandleUncached(
	const engine::rendering::Material::Handle &materialHandle,
	const WebGPUMaterialOptions &options
)
{
	auto materialOpt = materialHandle.get();
	if (!materialOpt || !materialOpt.value())
	{
		throw std::runtime_error("Invalid material handle in WebGPUMaterialFactory::getOrCreateFromHandle");
	}

	const auto &material = *materialOpt.value();

	// Get the material properties
	const auto &materialProps = material.getProperties();

	// Get texture slots (new API with ColorSpace)
	auto textureSlots = material.getTextureSlots();
	std::unordered_map<std::string, std::shared_ptr<WebGPUTexture>> textureMap;
	for(const auto& [slotName, texSlot] : textureSlots)
	{
		if (!texSlot.isValid())
		{
			throw std::runtime_error("Invalid texture handle for slot " + slotName + " in material ID " + std::to_string(material.getId()));
		}
		// Use the color space from the texture slot
		textureMap[slotName] = getTextureView(m_context, texSlot.handle, std::nullopt, texSlot.colorSpace);
	}

	auto webgpuMaterial = std::make_shared<WebGPUMaterial>(
		m_context,
		materialHandle,
		textureMap,
		options
	);
	
	// Update GPU resources immediately after creation
	webgpuMaterial->updateGPUResources();
	
	return webgpuMaterial;
}

std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFromHandleUncached(
	const engine::rendering::Material::Handle &handle
)
{
	return createFromHandleUncached(handle, WebGPUMaterialOptions{});
}

} // namespace engine::rendering::webgpu
