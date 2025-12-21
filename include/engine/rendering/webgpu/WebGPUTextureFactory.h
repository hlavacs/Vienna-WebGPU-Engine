
#pragma once

// Hash specialization for tuple used as color texture cache key
#include <tuple>
#include <cstdint>
#include <functional>
namespace std {
template<>
struct hash<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>> {
	size_t operator()(const std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>& t) const noexcept {
		size_t h = 0;
		h ^= std::hash<uint8_t>{}(std::get<0>(t)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(std::get<1>(t)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(std::get<2>(t)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(std::get<3>(t)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint32_t>{}(std::get<4>(t)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint32_t>{}(std::get<5>(t)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};
}

#include <glm/glm.hpp>
#include <memory>

#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUTextureFactory : public BaseWebGPUFactory<engine::rendering::Texture, WebGPUTexture>
{
  public:
	using BaseWebGPUFactory::BaseWebGPUFactory;

	explicit WebGPUTextureFactory(WebGPUContext &context);

	/**
	 * @brief Create a WebGPUTexture from a color.
	 * @param color The color to use.
	 * @param width Width of the texture.
	 * @param height Height of the texture.
	 * @return Shared pointer to WebGPUTexture.
	 */
	std::shared_ptr<WebGPUTexture> createFromColor(
		const glm::vec3 &color,
		uint32_t width = 1,
		uint32_t height = 1
	);

	/**
	 * @brief Create a WebGPUTexture from explicit descriptors.
	 * @param textureDesc Texture descriptor.
	 * @param viewDesc Texture view descriptor.
	 * @return Shared pointer to WebGPUTexture.
	 */
	std::shared_ptr<WebGPUTexture> createFromDescriptors(
		const wgpu::TextureDescriptor &textureDesc,
		const wgpu::TextureViewDescriptor &viewDesc
	);

	/**
	 * @brief Get the default white texture.
	 * @return Shared pointer to the white texture.
	 */
	std::shared_ptr<WebGPUTexture> getWhiteTexture();

	/**
	 * @brief Get the default normal texture.
	 * @return Shared pointer to the normal texture.
	 */
	std::shared_ptr<WebGPUTexture> getDefaultNormalTexture();

	void cleanup() override {
		m_whiteTexture.reset();
		m_defaultNormalTexture.reset();
		m_colorTextureCache.clear();
		BaseWebGPUFactory::cleanup();
	}

  protected:
	/**
	 * @brief Create a WebGPUTexture from a Texture handle.
	 * @param handle Handle to the Texture.
	 * @return Shared pointer to WebGPUTexture.
	 */
	std::shared_ptr<WebGPUTexture> createFromHandleUncached(
		const engine::rendering::Texture::Handle &handle
	) override;

  private:
	std::shared_ptr<WebGPUTexture> m_whiteTexture;
	std::shared_ptr<WebGPUTexture> m_defaultNormalTexture;
	std::unordered_map<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>, std::shared_ptr<WebGPUTexture>> m_colorTextureCache;
};
} // namespace engine::rendering::webgpu
