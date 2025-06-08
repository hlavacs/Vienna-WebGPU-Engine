#include "engine/resources/MaterialManager.h"

namespace engine::resources
{
    MaterialManager::MaterialManager(std::shared_ptr<TextureManager> textureManager)
        : m_textureManager(std::move(textureManager)) {}

    std::optional<MaterialManager::MaterialPtr> MaterialManager::createMaterial(const tinyobj::material_t& objMat, const std::string& textureBasePath)
    {
        auto mat = std::make_shared<Material>();
        mat->albedoColor = glm::vec3(objMat.diffuse[0], objMat.diffuse[1], objMat.diffuse[2]);
        mat->emissiveColor = glm::vec3(objMat.emission[0], objMat.emission[1], objMat.emission[2]);
        mat->specularColor = glm::vec3(objMat.specular[0], objMat.specular[1], objMat.specular[2]);
        mat->metallic = objMat.metallic;
        mat->roughness = objMat.roughness;
        mat->shininess = objMat.shininess;
        mat->opacity = objMat.dissolve;
        mat->setName(objMat.name);
		
        // TODO: support for additional tinyobj::material_t texture slots if needed (e.g. bump_texname, displacement_texname, reflection_texname, etc.)
        mat->albedoTexture = m_textureManager->createTextureFromFile(textureBasePath + objMat.diffuse_texname).value_or(TextureHandle{});
        mat->normalTexture = m_textureManager->createTextureFromFile(textureBasePath + objMat.normal_texname).value_or(TextureHandle{});
        mat->metallicTexture = m_textureManager->createTextureFromFile(textureBasePath + objMat.metallic_texname).value_or(TextureHandle{});
        mat->roughnessTexture = m_textureManager->createTextureFromFile(textureBasePath + objMat.roughness_texname).value_or(TextureHandle{});
        mat->aoTexture = m_textureManager->createTextureFromFile(textureBasePath + objMat.ambient_texname).value_or(TextureHandle{});
        mat->emissiveTexture = m_textureManager->createTextureFromFile(textureBasePath + objMat.emissive_texname).value_or(TextureHandle{});
        mat->opacityTexture = m_textureManager->createTextureFromFile(textureBasePath + objMat.alpha_texname).value_or(TextureHandle{});
        mat->specularTexture = m_textureManager->createTextureFromFile(textureBasePath + objMat.specular_texname).value_or(TextureHandle{});

		// TODO: respect other texture slots like displacement_texname, reflection_texname, etc. if needed
        auto handleOpt = add(mat);
        if (!handleOpt)
            return std::nullopt;
        return mat;
    }

    std::shared_ptr<TextureManager> MaterialManager::getTextureManager() const
    {
        return m_textureManager;
    }

} // namespace engine::resources
