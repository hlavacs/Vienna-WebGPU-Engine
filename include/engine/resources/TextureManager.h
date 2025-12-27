#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/rendering/Texture.h"
#include "engine/resources/ResourceManagerBase.h"
#include "engine/resources/loaders/ImageLoader.h"

namespace engine::resources
{

/**
 * @class TextureManager
 * @brief Manages creation, storage, and retrieval of textures within the engine.
 *
 * Supports different texture types: Image (from file or raw data), DepthStencil, and Surface.
 * Provides caching for image textures by file path, and stores created depth/surface textures
 * internally for bookkeeping. Thread-safe via internal mutex.
 */
class TextureManager : public ResourceManagerBase<engine::rendering::Texture>
{
  public:
	using path = std::filesystem::path;
	using TextureHandle = engine::rendering::Texture::Handle;
	using TexturePtr = std::shared_ptr<engine::rendering::Texture>;

	/**
	 * @brief Constructs a TextureManager with a given ImageLoader.
	 * @param loader Shared pointer to an ImageLoader.
	 */
	explicit TextureManager(std::shared_ptr<engine::resources::loaders::ImageLoader> loader) : m_loader(std::move(loader)) {}

	/**
	 * @brief Creates an Image texture from raw pixel data.
	 * @param width Texture width.
	 * @param height Texture height.
	 * @param channels Number of channels.
	 * @param pixels Pixel data vector.
	 * @param filePath Optional file path for caching.
	 * @return Optional shared pointer to the texture, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<TexturePtr> createImageTexture(
		engine::resources::Image::Ptr image, std::optional<path> filePath = std::nullopt
	);

	/**
	 * @brief Creates a depth-stencil texture.
	 * @param width Texture width.
	 * @param height Texture height.
	 * @return Optional shared pointer to the texture, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<TexturePtr> createDepthTexture(uint32_t width, uint32_t height);

	/**
	 * @brief Creates a surface texture (color target).
	 * @param width Texture width.
	 * @param height Texture height.
	 * @return Optional shared pointer to the texture, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<TexturePtr> createSurfaceTexture(uint32_t width, uint32_t height, uint32_t channels = 4);

	/**
	 * @brief Loads an image texture from file or returns cached one.
	 * @param filepath Path to the image file.
	 * @param forceReload If true, reloads the texture even if cached.
	 * @return Optional shared pointer to the texture, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<TexturePtr> createTextureFromFile(const path &filepath, bool forceReload = false);

	/**
	 * @brief Retrieves a cached image texture by its file path.
	 * @param filepath Path to the image file.
	 * @return Optional shared pointer to the texture, or std::nullopt if not found.
	 */
	[[nodiscard]]
	std::optional<TexturePtr> getTextureByPath(const path &filepath) const;

  private:
	std::shared_ptr<engine::resources::loaders::ImageLoader> m_loader;

	// Caches
	std::unordered_map<std::string, TextureHandle> m_imageCache; ///< Image textures cached by absolute file path
	std::vector<TextureHandle> m_depthCache;					 ///< DepthStencil textures
	std::vector<TextureHandle> m_surfaceCache;					 ///< Surface textures
};

} // namespace engine::resources
