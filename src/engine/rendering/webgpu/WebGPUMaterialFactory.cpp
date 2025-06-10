#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/Material.h"
#include <memory>

namespace engine::rendering::webgpu
{

	WebGPUMaterialFactory::WebGPUMaterialFactory(WebGPUContext &context)
		: BaseWebGPUFactory(context) {}

	std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFrom(
		const engine::rendering::Material &material,
		// const WebGPUTexture& baseColor,
		// const WebGPUTexture& normalMap,
		const WebGPUMaterialOptions &options)
	{
		// Build bind group
		wgpu::BindGroupDescriptor desc{};
		desc.layout = options.bindGroupLayout;
		// wgpu::BindGroupEntry entries[2] = {};
		// entries[0].binding = 0;
		// entries[0].textureView = baseColor.getTextureView();
		// entries[1].binding = 1;
		// entries[1].textureView = normalMap.getTextureView();
		desc.entryCount = 2;
		desc.entries = nullptr; // TODO
		wgpu::BindGroup bindGroup = m_context.getDevice().createBindGroup(desc);
		return std::make_shared<WebGPUMaterial>(bindGroup, options);
	}

	std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFrom(const engine::rendering::Material &material)
	{
		return createFrom(material, WebGPUMaterialOptions{});
	}

} // namespace engine::rendering::webgpu
