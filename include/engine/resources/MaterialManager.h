#pragma once

#include "engine/io/tiny_obj_loader.h"
#include "engine/rendering/Material.h"
#include "engine/resources/ResourceManagerBase.h"
#include "engine/resources/TextureManager.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::resources
{
/**
 * @class MaterialManager
 * @brief Manages creation, storage, and retrieval of materials within the engine.
 *
 * Provides facilities to add and retrieve materials, deduplicate by name, and resolve texture handles
 * using the TextureManager. Materials are stored as shared pointers for safe usage across subsystems.
 */
class MaterialManager : public ResourceManagerBase<engine::rendering::Material>
{
  public:
	using Material = engine::rendering::Material;
	using MaterialHandle = Material::Handle;
	using MaterialPtr = std::shared_ptr<Material>;
	using TextureHandle = engine::rendering::Texture::Handle;

	/**
	 * @brief Constructs a MaterialManager with a given TextureManager.
	 * @param textureManager Shared pointer to a TextureManager.
	 */
	explicit MaterialManager(std::shared_ptr<TextureManager> textureManager);

	/**
	 * @brief Creates a Material from a tinyobj::material_t and adds it to the manager.
	 * @param objMat The tinyobj material.
	 * @param textureBasePath The base path for texture files.
	 * @return Optional shared pointer to the created material, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<MaterialPtr> createMaterial(const tinyobj::material_t &objMat, const std::string &textureBasePath = "");

	/**
	 * @brief Access the underlying TextureManager.
	 * @return Shared pointer to the TextureManager.
	 */
	[[nodiscard]]
	std::shared_ptr<TextureManager> getTextureManager() const;

  private:
	std::shared_ptr<TextureManager> m_textureManager;
};

} // namespace engine::resources
