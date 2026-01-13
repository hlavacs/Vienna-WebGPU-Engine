#pragma once

#include <memory>
#include <optional>
#include <string>
#include <tiny_gltf.h>
#include <unordered_map>
#include <vector>

#include "engine/io/tiny_obj_loader.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MaterialFeatureMask.h"
#include "engine/resources/ResourceManagerBase.h"
#include "engine/resources/TextureManager.h"

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
	using MaterialFeature = engine::rendering::MaterialFeature;
	using MaterialHandle = Material::Handle;
	using MaterialPtr = std::shared_ptr<Material>;
	using TextureHandle = engine::rendering::Texture::Handle;

	/**
	 * @brief Constructs a MaterialManager with a given TextureManager.
	 * @param textureManager Shared pointer to a TextureManager.
	 */
	explicit MaterialManager(std::shared_ptr<TextureManager> textureManager) :
		m_textureManager(std::move(textureManager)) {}

	/**
	 * @brief Creates a Material from a tinyobj::material_t and adds it to the manager.
	 * @param objMat The tinyobj material.
	 * @param textureBasePath The base path for texture files.
	 * @return Optional shared pointer to the created material, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<MaterialPtr> createMaterial(const tinyobj::material_t &objMat, const std::string &textureBasePath = "");

	/**
	 * @brief Creates a Material from a tinygltf::Material and adds it to the manager.
	 * @param gltfMat The tinygltf material.
	 * @param textures The array of tinygltf textures.
	 * @param images The array of tinygltf images.
	 * @param textureBasePath The base path for texture files.
	 * @return Optional shared pointer to the created material, or std::nullopt on failure.
	 */
	[[nodiscard]]
	std::optional<MaterialPtr> createMaterial(
		const tinygltf::Material &gltfMat,
		const std::vector<tinygltf::Texture> &textures,
		const std::vector<tinygltf::Image> &images,
		const std::string &textureBasePath = ""
	);

	/**
	 * @brief Access the underlying TextureManager.
	 * @return Shared pointer to the TextureManager.
	 */
	[[nodiscard]]
	std::shared_ptr<TextureManager> getTextureManager() const;

	MaterialHandle getDefaultMaterial() const;

  private:
	mutable MaterialHandle m_defaultMaterial;
	std::shared_ptr<TextureManager> m_textureManager;
};

} // namespace engine::resources
