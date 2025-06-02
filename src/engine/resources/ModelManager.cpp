#include "engine/resources/MaterialManager.h"

namespace engine::resources
{
    MaterialManager::MaterialManager(std::shared_ptr<TextureManager> textureManager)
        : m_textureManager(std::move(textureManager)) {}

    std::optional<MaterialManager::MaterialHandle> MaterialManager::add(
        const MaterialPtr& material,
        const std::unordered_map<std::string, std::string>& textureFilePaths
    ) {
        if (!material)
            return std::nullopt;

        for (const auto& [slot, file] : textureFilePaths)
        {
            if (!file.empty() && m_textureManager)
            {
                auto texPtrOpt = m_textureManager->createTextureFromFile(file);
                if (texPtrOpt && *texPtrOpt)
                {
                    const auto& texHandle = (*texPtrOpt)->getHandle();
                    if (slot == "albedo") material->albedoTexture = texHandle;
                    else if (slot == "normal") material->normalTexture = texHandle;
                    else if (slot == "metallic") material->metallicTexture = texHandle;
                    else if (slot == "roughness") material->roughnessTexture = texHandle;
                    else if (slot == "ao") material->aoTexture = texHandle;
                    else if (slot == "emissive") material->emissiveTexture = texHandle;
                    else if (slot == "opacity") material->opacityTexture = texHandle;
                    else if (slot == "specular") material->specularTexture = texHandle;
                }
            }
        }

        return ResourceManagerBase<Material>::add(material);
    }

    std::optional<MaterialManager::MaterialPtr> MaterialManager::getMaterial(const MaterialHandle& handle) const
    {
        return this->get(handle);
    }

    std::shared_ptr<TextureManager> MaterialManager::getTextureManager() const
    {
        return m_textureManager;
    }

} // namespace engine::resources
