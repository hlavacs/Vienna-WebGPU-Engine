#pragma once
#include <memory>
#include <glm/glm.hpp>

#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/Texture.h"

namespace engine::rendering::webgpu
{
	class WebGPUContext;

	class WebGPUTextureFactory : public BaseWebGPUFactory<engine::rendering::Texture, WebGPUTexture>
	{
	public:
		using BaseWebGPUFactory::BaseWebGPUFactory;

		explicit WebGPUTextureFactory(WebGPUContext &context);
		std::shared_ptr<WebGPUTexture> createFrom(const engine::rendering::Texture &texture) override;
		std::shared_ptr<WebGPUTexture> createFromColor(const glm::vec3 &color, uint32_t width, uint32_t height);

		// Persistent fallback textures
		wgpu::TextureView getWhiteTextureView();
		wgpu::TextureView getDefaultNormalTextureView();

	private:
		std::shared_ptr<WebGPUTexture> m_whiteTexture;
		std::shared_ptr<WebGPUTexture> m_defaultNormalTexture;
	};

} // namespace engine::rendering::webgpu
