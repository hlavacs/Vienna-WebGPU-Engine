#include "engine/resources/MaterialManager.h"
#include "engine/core/Handle.h"

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
        using engine::core::unwrapOrHandle;
        mat->albedoTexture = unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.diffuse_texname));
        mat->normalTexture = unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.normal_texname));
        mat->metallicTexture = unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.metallic_texname));
        mat->roughnessTexture = unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.roughness_texname));
        mat->aoTexture = unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.ambient_texname));
        mat->emissiveTexture = unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.emissive_texname));
        mat->opacityTexture = unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.alpha_texname));
        mat->specularTexture = unwrapOrHandle(m_textureManager->createTextureFromFile(textureBasePath + objMat.specular_texname));

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
