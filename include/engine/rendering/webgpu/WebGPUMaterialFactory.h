#pragma once
#include <memory>
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/Material.h"

namespace engine::rendering::webgpu
{
	class WebGPUMaterialFactory : public BaseWebGPUFactory<engine::rendering::Material, WebGPUMaterial>
	{
	public:
		using BaseWebGPUFactory::BaseWebGPUFactory;

		explicit WebGPUMaterialFactory(WebGPUContext &context);

		std::shared_ptr<WebGPUMaterial> createFrom(
			const engine::rendering::Material &material,
			// const WebGPUTexture &baseColor,
			// const WebGPUTexture &normalMap,
			const WebGPUMaterialOptions &options);

		// This is required to satisfy the abstract base class:
		std::shared_ptr<WebGPUMaterial> createFrom(const engine::rendering::Material &material) override;
	};
} // namespace engine::rendering::webgpu
