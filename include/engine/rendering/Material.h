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

        // Getters
        glm::vec3 getAlbedoColor() const { return albedoColor; }
        glm::vec3 getEmissiveColor() const { return emissiveColor; }
        glm::vec3 getSpecularColor() const { return specularColor; }
        float getMetallic() const { return metallic; }
        float getRoughness() const { return roughness; }
        float getShininess() const { return shininess; }
        float getOpacity() const { return opacity; }

        TextureHandle getAlbedoTexture() const { return albedoTexture; }
        TextureHandle getNormalTexture() const { return normalTexture; }
        TextureHandle getMetallicTexture() const { return metallicTexture; }
        TextureHandle getRoughnessTexture() const { return roughnessTexture; }
        TextureHandle getAOTexture() const { return aoTexture; }
        TextureHandle getEmissiveTexture() const { return emissiveTexture; }
        TextureHandle getOpacityTexture() const { return opacityTexture; }
        TextureHandle getSpecularTexture() const { return specularTexture; }

        // Setters
        void setAlbedoColor(const glm::vec3& c) { albedoColor = c; }
        void setEmissiveColor(const glm::vec3& c) { emissiveColor = c; }
        void setSpecularColor(const glm::vec3& c) { specularColor = c; }
        void setMetallic(float v) { metallic = v; }
        void setRoughness(float v) { roughness = v; }
        void setShininess(float v) { shininess = v; }
        void setOpacity(float v) { opacity = v; }

        void setAlbedoTexture(TextureHandle t) { albedoTexture = t; }
        void setNormalTexture(TextureHandle t) { normalTexture = t; }
        void setMetallicTexture(TextureHandle t) { metallicTexture = t; }
        void setRoughnessTexture(TextureHandle t) { roughnessTexture = t; }
        void setAOTexture(TextureHandle t) { aoTexture = t; }
        void setEmissiveTexture(TextureHandle t) { emissiveTexture = t; }
        void setOpacityTexture(TextureHandle t) { opacityTexture = t; }
        void setSpecularTexture(TextureHandle t) { specularTexture = t; }

        // Texture presence checks
        bool hasAlbedoTexture() const { return albedoTexture.valid(); }
        bool hasNormalTexture() const { return normalTexture.valid(); }
        bool hasMetallicTexture() const { return metallicTexture.valid(); }
        bool hasRoughnessTexture() const { return roughnessTexture.valid(); }
        bool hasAOTexture() const { return aoTexture.valid(); }
        bool hasEmissiveTexture() const { return emissiveTexture.valid(); }
        bool hasOpacityTexture() const { return opacityTexture.valid(); }
        bool hasSpecularTexture() const { return specularTexture.valid(); }

    private:
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
    };

} // namespace engine::rendering
