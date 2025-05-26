#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <optional>
#include <filesystem>
#include "engine/rendering/Texture.h"
#include "engine/resources/TextureLoader.h"

namespace engine::resources
{
	/**
	 * @class TextureManager
	 * @brief Manages creation, storage, and retrieval of textures within the engine.
	 *
	 * This class provides facilities to create textures from raw data or from files,
	 * keeps track of loaded textures, and allows querying by various identifiers
	 * such as handle, runtime ID, file path, or human-readable name.
	 *
	 * Textures are stored internally as shared pointers to allow safe shared usage
	 * across different subsystems without unnecessary copying.
	 *
	 * The manager maintains mappings between texture handles, file paths, and texture
	 * objects to avoid duplicate loads and enable efficient resource management.
	 */
	class TextureManager
	{
	public:
		using path = std::filesystem::path;
		using TextureHandle = engine::rendering::Texture::Handle;
		using TexturePtr = std::shared_ptr<engine::rendering::Texture>;
		using TextureResult = std::pair<TextureHandle, TexturePtr>; // ToDo: Not pair but define a Handle + Ptr on Identifiable

		explicit TextureManager(std::shared_ptr<engine::resources::TextureLoader> loader)
			: m_loader(std::move(loader)) {}

		// Create a texture from raw data, returns optional handle with shared_ptr and id
		[[nodiscard]]
		std::optional<TextureResult> createTexture(uint32_t width, uint32_t height, uint32_t channels, std::vector<uint8_t> &&pixels);

		/**
		 * @brief Creates or loads a texture from a file path.
		 *
		 * If the texture for the given path is already loaded and forceReload is false,
		 * returns the existing texture handle and pointer.
		 *
		 * If forceReload is true, forces loading the texture from file,
		 * replacing any existing texture associated with that path.
		 *
		 * @param filepath The file path to load the texture from.
		 * @param forceReload If true, forces reloading the texture even if already loaded.
		 * @return Optional TextureResult containing the handle and shared pointer on success, std::nullopt on failure.
		 */
		[[nodiscard]]
		std::optional<TextureResult> createTextureFromFile(const path &filepath, bool forceReload = false);

		/**
		 * @brief Retrieves a texture by its unique runtime ID.
		 *
		 * Searches the texture storage for a texture matching the given ID.
		 *
		 * @param id The unique ID of the texture.
		 * @return Optional shared pointer to the texture if found, std::nullopt otherwise.
		 */
		[[nodiscard]]
		std::optional<TexturePtr> getTextureByID(uint64_t id) const;

		/**
		 * @brief Retrieves a texture by its handle.
		 *
		 * Looks up the texture associated with the given handle.
		 *
		 * @param handle The handle uniquely identifying the texture.
		 * @return Optional shared pointer to the texture if found, std::nullopt otherwise.
		 */
		[[nodiscard]]
		std::optional<TexturePtr> getTextureByHandle(TextureHandle handle) const;

		/**
		 * @brief Retrieves a texture by its file path.
		 *
		 * Finds a texture loaded from the specified file path.
		 * Returns std::nullopt if no texture for the path is found.
		 *
		 * @param filepath The path to the texture file.
		 * @return Optional shared pointer to the texture if found, std::nullopt otherwise.
		 */
		[[nodiscard]]
		std::optional<TexturePtr> getTextureByPath(const path &filepath) const;

		/**
		 * @brief Retrieves a texture by its human-readable name.
		 *
		 * Searches for a texture by its assigned name.
		 * Returns the first matching texture if multiple exist with the same name.
		 *
		 * @param name The name of the texture.
		 * @return Optional shared pointer to the texture if found, std::nullopt otherwise.
		 */
		[[nodiscard]]
		std::optional<TexturePtr> getTextureByName(const std::string &name) const;

	private:
		std::shared_ptr<engine::resources::TextureLoader> m_loader;
		std::unordered_map<TextureHandle, TexturePtr> m_textures;
		std::unordered_map<std::string, TextureHandle> m_pathToTexture;
	};

} // namespace engine::resources
