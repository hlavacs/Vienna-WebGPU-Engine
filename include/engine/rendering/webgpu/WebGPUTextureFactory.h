
#pragma once

// Hash specialization for tuple used as color texture cache key
#include <cstdint>
#include <functional>
#include <tuple>
namespace std
{
template <>
struct hash<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>>
{
	size_t operator()(const std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t> &t) const noexcept
	{
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
} // namespace std

#include <glm/glm.hpp>
#include <memory>

#include "engine/rendering/Texture.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

struct WebGPUTextureOptions
{
	std::optional<wgpu::TextureFormat> format = wgpu::TextureFormat::Undefined; // optional override, default determined automatically
	std::optional<wgpu::TextureUsage> usage = wgpu::TextureUsage::None;		 // optional override
	bool generateMipmaps{true};									 // default on
};

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
	 * @brief Get the default black texture.
	 * @return Shared pointer to the black texture.
	 */
	std::shared_ptr<WebGPUTexture> getBlackTexture();

	/**
	 * @brief Get the default normal texture.
	 * @return Shared pointer to the normal texture.
	 */
	std::shared_ptr<WebGPUTexture> getDefaultNormalTexture();

	void cleanup() override
	{
		m_whiteTexture.reset();
		m_defaultNormalTexture.reset();
		m_colorTextureCache.clear();
		BaseWebGPUFactory::cleanup();
	}

	/**
	 * @brief Get or create a WebGPUTexture from a Texture handle with options.
	 * @param handle Handle to the Texture.
	 * @param options Options for texture creation.
	 * @return Shared pointer to WebGPUTexture.
	 */
	std::shared_ptr<WebGPUTexture> createFromHandle(
		const engine::rendering::Texture::Handle &handle,
		const WebGPUTextureOptions &options
	)
	{
		auto it = m_cache.find(handle);
		if (it != m_cache.end())
		{
			return it->second;
		}
		auto product = createFromHandleUncached(handle, options);
		m_cache[handle] = product;
		return product;
	}

  protected:
	std::shared_ptr<WebGPUTexture> createFromHandleUncached(
		const engine::rendering::Texture::Handle &handle
	) override
	{
		WebGPUTextureOptions options{};
		return createFromHandleUncached(handle, options);
	}

	std::shared_ptr<WebGPUTexture> createFromHandleUncached(
		const engine::rendering::Texture::Handle &handle,
		const WebGPUTextureOptions &options
	);

	void uploadTextureData(const Texture& texture, wgpu::Texture& gpuTexture);
	void generateMipmaps(wgpu::Texture& texture, wgpu::TextureFormat format, uint32_t width, uint32_t height, uint32_t mipLevelCount);
	
  private:
	std::shared_ptr<WebGPUTexture> m_whiteTexture;
	std::shared_ptr<WebGPUTexture> m_blackTexture;
	std::shared_ptr<WebGPUTexture> m_defaultNormalTexture;
	std::unordered_map<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>, std::shared_ptr<WebGPUTexture>> m_colorTextureCache;
};
} // namespace engine::rendering::webgpu
