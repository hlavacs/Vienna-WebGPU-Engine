#pragma once
#include <memory>
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/Texture.h"

namespace engine::rendering::webgpu
{

	class WebGPUTextureFactory : public BaseWebGPUFactory<engine::rendering::Texture, WebGPUTexture>
	{
	public:
		using BaseWebGPUFactory::BaseWebGPUFactory;

		explicit WebGPUTextureFactory(WebGPUContext &context);
		std::shared_ptr<WebGPUTexture> createFrom(const engine::rendering::Texture &texture) override;
	};

} // namespace engine::rendering::webgpu
