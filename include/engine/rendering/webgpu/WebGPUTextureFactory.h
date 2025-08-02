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

		/**
		 * @brief Create a WebGPUTexture from a Texture handle.
		 * @param handle Handle to the Texture.
		 * @return Shared pointer to WebGPUTexture.
		 */
		std::shared_ptr<WebGPUTexture> createFromHandle(
			const engine::rendering::Texture::Handle &handle) override;

		/**
		 * @brief Create a WebGPUTexture from a color.
		 * @param color The color to use.
		 * @param width Width of the texture.
		 * @param height Height of the texture.
		 * @param options Optional texture options.
		 * @return Shared pointer to WebGPUTexture.
		 */
		std::shared_ptr<WebGPUTexture> createFromColor(
			const glm::vec3 &color,
			uint32_t width = 1,
			uint32_t height = 1,
			const WebGPUTextureOptions &options = {});

		/**
		 * @brief Get the default white texture view.
		 * @return The white texture view.
		 */
		wgpu::TextureView getWhiteTextureView();

		/**
		 * @brief Get the default normal texture view.
		 * @return The normal texture view.
		 */
		wgpu::TextureView getDefaultNormalTextureView();

	private:
		std::shared_ptr<WebGPUTexture> m_whiteTexture;
		std::shared_ptr<WebGPUTexture> m_defaultNormalTexture;
	};

} // namespace engine::rendering::webgpu
