#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <optional>
#include <vector>
#include "engine/io/tiny_obj_loader.h"
#include "engine/rendering/Material.h"
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
        using MaterialHandle = Material::Handle;
        using MaterialPtr = std::shared_ptr<Material>;
        using TextureHandle = engine::rendering::Texture::Handle;

        /**
         * @brief Constructs a MaterialManager with a given TextureManager.
         * @param textureManager Shared pointer to a TextureManager.
         */
        explicit MaterialManager(std::shared_ptr<TextureManager> textureManager);

        /**
         * @brief Adds a material to the manager, optionally resolving texture file paths.
         * @param material Shared pointer to the material.
         * @param textureFilePaths Optional map from material texture slot name to file path. If provided, will load textures via TextureManager.
         * @return Optional handle to the material, or std::nullopt if material is null.
         */
        [[nodiscard]]
        std::optional<MaterialHandle> add(
            const MaterialPtr& material,
            const std::unordered_map<std::string, std::string>& textureFilePaths = {}
        );

        /**
         * @brief Retrieves a material by its human-readable name.
         * @param name The name of the material.
         * @return Optional shared pointer to the material if found, std::nullopt otherwise.
         */
        [[nodiscard]]
        std::optional<MaterialPtr> getMaterialByName(const std::string& name) const;

        /**
         * @brief Retrieves a material by its handle.
         * @param handle The handle of the material.
         * @return Optional shared pointer to the material if found, std::nullopt otherwise.
         */
        [[nodiscard]]
        std::optional<MaterialPtr> getMaterial(const MaterialHandle& handle) const;

        /**
         * @brief Creates a Material from a tinyobj::material_t and adds it to the manager.
         * @param objMat The tinyobj material.
         * @param textureBasePath The base path for texture files.
         * @return Optional shared pointer to the created material, or std::nullopt on failure.
         */
        [[nodiscard]]
        std::optional<MaterialPtr> createMaterial(const tinyobj::material_t& objMat, const std::string& textureBasePath = "");

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
