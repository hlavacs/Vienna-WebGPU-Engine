#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include "engine/core/Identifiable.h"
#include "engine/core/Handle.h"
#include "engine/rendering/Texture.h"

namespace engine::rendering
{
    using TextureHandle = Texture::Handle;

    struct Material : public engine::core::Identifiable<Material>
    {
        using Handle = engine::core::Handle<Material>;
        using Ptr = std::shared_ptr<Material>;

        // Constructors
        Material() = default;

        Material(const glm::vec3& albedo, TextureHandle albedoTex, TextureHandle normalTex = {}, TextureHandle metallicTex = {},
                 TextureHandle roughnessTex = {}, TextureHandle aoTex = {}, TextureHandle emissiveTex = {}, TextureHandle opacityTex = {},
                 TextureHandle specularTex = {}, float metallicVal = 0.0f, float roughnessVal = 1.0f, float opacityVal = 1.0f)
            : albedoColor(albedo), metallic(metallicVal), roughness(roughnessVal), opacity(opacityVal),
              albedoTexture(albedoTex), normalTexture(normalTex), metallicTexture(metallicTex), roughnessTexture(roughnessTex),
              aoTexture(aoTex), emissiveTexture(emissiveTex), opacityTexture(opacityTex), specularTexture(specularTex)
        {}

        // Move only
        Material(Material&&) noexcept = default;
        Material& operator=(Material&&) noexcept = default;

        // No copy
        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;

        // Common PBR values
        glm::vec3 albedoColor = glm::vec3(1.0f);
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        glm::vec3 specularColor = glm::vec3(1.0f);
        float metallic = 0.0f;
        float roughness = 1.0f;
        float shininess = 32.0f;
        float opacity = 1.0f;

        // Common textures
        TextureHandle albedoTexture;
        TextureHandle normalTexture;
        TextureHandle metallicTexture;
        TextureHandle roughnessTexture;
        TextureHandle aoTexture;
        TextureHandle emissiveTexture;
        TextureHandle opacityTexture;
        TextureHandle specularTexture;

        // Texture presence checks
        bool hasAlbedoTexture() const { return albedoTexture.valid(); }
        bool hasNormalTexture() const { return normalTexture.valid(); }
        bool hasMetallicTexture() const { return metallicTexture.valid(); }
        bool hasRoughnessTexture() const { return roughnessTexture.valid(); }
        bool hasAOTexture() const { return aoTexture.valid(); }
        bool hasEmissiveTexture() const { return emissiveTexture.valid(); }
        bool hasOpacityTexture() const { return opacityTexture.valid(); }
        bool hasSpecularTexture() const { return specularTexture.valid(); }
    };

} // namespace engine::rendering
