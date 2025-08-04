#pragma once

#include "engine/rendering/Texture.h"
#include "engine/resources/ResourceManagerBase.h"
#include "engine/resources/loaders/TextureLoader.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace engine::resources
{
/**
 * @class TextureManager
 * @brief Manages creation, storage, and retrieval of textures within the engine.
 *
 * Provides facilities to create textures from raw data or files, keeps track of loaded textures,
 * and allows querying by handle, runtime ID, file path, or human-readable name.
 * Textures are stored as shared pointers for safe usage across subsystems.
 * File path mapping avoids duplicate loads and enables efficient resource management.
 */
class TextureManager : public ResourceManagerBase<engine::rendering::Texture>
{
  public:
	using path = std::filesystem::path;
	using TextureHandle = engine::rendering::Texture::Handle;
	using TexturePtr = std::shared_ptr<engine::rendering::Texture>;

	/**
	 * @brief Constructs a TextureManager with a given TextureLoader.
	 * @param loader Shared pointer to a TextureLoader.
	 */
	explicit TextureManager(std::shared_ptr<engine::resources::loaders::TextureLoader> loader) :
		m_loader(std::move(loader)) {}

	/**
	 * @brief Creates a texture from raw data.
	 * @param width Texture width.
	 * @param height Texture height.
	 * @param channels Number of color channels.
	 * @param pixels Raw pixel data.
	 * @return Optional shared pointer to the created texture, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<TexturePtr> createTexture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixels);

	/**
	 * @brief Creates or loads a texture from a file path.
	 *
	 * If the texture for the given path is already loaded and forceReload is false,
	 * returns the existing texture pointer.
	 * If forceReload is true, forces loading the texture from file,
	 * replacing any existing texture associated with that path.
	 *
	 * @param filepath The file path to load the texture from.
	 * @param forceReload If true, forces reloading the texture even if already loaded.
	 * @return Optional shared pointer to the texture on success, std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<TexturePtr> createTextureFromFile(const path &filepath, bool forceReload = false);

	/**
	 * @brief Retrieves a texture by its file path.
	 * @param filepath The path to the texture file.
	 * @return Optional shared pointer to the texture if found, std::nullopt otherwise.
	 */
	[[nodiscard]]
	std::optional<TexturePtr> getTextureByPath(const path &filepath) const;

  private:
	std::shared_ptr<engine::resources::loaders::TextureLoader> m_loader;
	std::unordered_map<std::string, TextureHandle> m_pathToTexture;
};

} // namespace engine::resources
