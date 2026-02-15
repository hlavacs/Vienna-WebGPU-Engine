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
	 * @brief Creates a PBR Material with specified properties and associated textures.
	 * @param name The name of the material.
	 * @param pbrProperties The PBR properties for the material.
	 * @param textures A map of texture slot names to texture handles.
	 */
	[[nodiscard]]
	std::optional<MaterialPtr> createPBRMaterial(
		std::string name,
		engine::rendering::PBRProperties pbrProperties,
		const std::unordered_map<std::string, TextureHandle> &textures
	);

	/**
	 * @brief Creates a Material with specified properties and associated textures.
	 * @param name The name of the material.
	 * @param properties The material properties data (type-erased).
	 * @param shader The shader to use for the material.
	 * @param textures A map of texture slot names to texture handles.
	 */
	template <typename T>
	[[nodiscard]]
	std::optional<MaterialPtr> createMaterial(
		std::string name,
		T properties,
		std::string shader,
		const std::unordered_map<std::string, TextureHandle> &textures
	)
	{
		auto mat = std::make_shared<Material>();

		mat->setName(name);
		mat->setProperties(properties);

		// Apply textures and determine features
		MaterialFeature::Flag features = applyTexturesAndGetFeatures(mat, textures);
		mat->setFeatureMask(features);

		// Set shader
		mat->setShader(shader);

		auto handleOpt = add(mat);
		if (!handleOpt)
			return std::nullopt;

		return mat;
	}

	/**
	 * @brief Access the underlying TextureManager.
	 * @return Shared pointer to the TextureManager.
	 */
	[[nodiscard]]
	std::shared_ptr<TextureManager> getTextureManager() const;

	MaterialHandle getDefaultMaterial() const;

  private:
	/**
	 * @brief Applies textures to a material and determines feature flags based on texture slots.
	 * @param mat The material to apply textures to.
	 * @param textures A map of texture slot names to texture handles.
	 * @return MaterialFeature flags indicating which texture maps are used.
	 */
	MaterialFeature::Flag applyTexturesAndGetFeatures(
		MaterialPtr mat,
		const std::unordered_map<std::string, TextureHandle> &textures
	);

	mutable MaterialHandle m_defaultMaterial;
	std::shared_ptr<TextureManager> m_textureManager;
};

} // namespace engine::resources
