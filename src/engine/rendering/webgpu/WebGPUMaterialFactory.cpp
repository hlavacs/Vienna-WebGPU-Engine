#include "engine/rendering/webgpu/WebGPUMaterialFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

#include <memory>
#include <stdexcept>

#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

	WebGPUMaterialFactory::WebGPUMaterialFactory(WebGPUContext &context)
		: BaseWebGPUFactory(context) {}

	std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFromHandle(
		const engine::rendering::Material::Handle &handle,
		const WebGPUMaterialOptions &options)
	{
		auto materialOpt = handle.get();
		if (!materialOpt || !materialOpt.value()) {
			throw std::runtime_error("Invalid material handle in WebGPUMaterialFactory::createFromHandle");
		}

		const auto& material = *materialOpt.value();
		auto& texFactory = m_context.textureFactory();
		wgpu::TextureView albedoView = nullptr;

		if (material.hasAlbedoTexture()) {
			auto texOpt = material.getAlbedoTexture().get();
			albedoView = (texOpt && texOpt.value())
				? texFactory.createFrom(*texOpt.value())->getTextureView()
				: texFactory.getWhiteTextureView();
		} else {
			glm::vec3 color = material.getAlbedoColor();
			albedoView = (color != glm::vec3(1.0f))
				? texFactory.createFromColor(color, 1, 1)->getTextureView()
				: texFactory.getWhiteTextureView();
		}

		wgpu::TextureView normalView = nullptr;
		if (material.hasNormalTexture()) {
			auto texHandle = material.getNormalTexture();
			auto texOpt = texHandle.get();
			if (texOpt && texOpt.value()) {
				normalView = texFactory.createFrom(*texOpt.value())->getTextureView();
			} else {
				normalView = texFactory.getDefaultNormalTextureView();
			}
		} else {
			normalView = texFactory.getDefaultNormalTextureView();
		}

		wgpu::BindGroupLayout bindGroupLayout = m_context.bindGroupFactory().createDefaultMaterialBindGroupLayout();
		auto bindGroup = m_context.bindGroupFactory().createMaterialBindGroup(
			bindGroupLayout,
			albedoView,
			normalView,
			m_context.getDefaultSampler()
		);

		return std::make_shared<WebGPUMaterial>(m_context, handle, bindGroup, options);
	}

	std::shared_ptr<WebGPUMaterial> WebGPUMaterialFactory::createFromHandle(
		const engine::rendering::Material::Handle &handle)
	{
		return createFromHandle(handle, WebGPUMaterialOptions{});
	}

} // namespace engine::rendering::webgpu
