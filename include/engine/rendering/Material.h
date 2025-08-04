#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include "engine/core/Identifiable.h"
#include "engine/core/Handle.h"
#include "engine/core/Versioned.h"
#include "engine/rendering/Texture.h"

namespace engine::rendering
{
	using TextureHandle = Texture::Handle;

	struct Material : public engine::core::Identifiable<Material>,
					  public engine::core::Versioned
	{

		struct MaterialProperties
		{
			float diffuse[3] = { 1.0f, 1.0f, 1.0f };
			float _pad0;
			float specular[3] = { 0.0f, 0.0f, 0.0f };
			float _pad1;
			float transmittance[3] = { 0.0f, 0.0f, 0.0f };
			float _pad2;
			float emission[3] = { 0.0f, 0.0f, 0.0f };
			float _pad3;
			float shininess = 32.0f;
			float ior = 1.5f;
			float opacity = 1.0f;
			float roughness = 0.5f;
			float metallic = 0.0f;
			float _pad[3] = { 0.0f, 0.0f, 0.0f }; // padding to ensure 16-byte alignment
		};
		static_assert(sizeof(MaterialProperties) % 16 == 0, "MaterialProperties must be 16-byte aligned");

		
		using Handle = engine::core::Handle<Material>;
		using Ptr = std::shared_ptr<Material>;

		// Constructors
		Material() = default;

		Material(const MaterialProperties& props,
				 TextureHandle albedoTex = {}, TextureHandle normalTex = {}, TextureHandle metallicTex = {},
				 TextureHandle roughnessTex = {}, TextureHandle aoTex = {}, TextureHandle emissiveTex = {}, TextureHandle opacityTex = {},
				 TextureHandle specularTex = {})
			: properties(props),
			  albedoTexture(albedoTex), normalTexture(normalTex), metallicTexture(metallicTex), roughnessTexture(roughnessTex),
			  aoTexture(aoTex), emissiveTexture(emissiveTex), opacityTexture(opacityTex), specularTexture(specularTex)
		{
		}

		// Move only
		Material(Material &&) noexcept = default;
		Material &operator=(Material &&) noexcept = default;

		// No copy
		Material(const Material &) = delete;
		Material &operator=(const Material &) = delete;

		// Getters for properties
		const MaterialProperties& getProperties() const { return properties; }
		void setProperties(const MaterialProperties& p) { properties = p; incrementVersion(); }

		TextureHandle getAlbedoTexture() const { return albedoTexture; }
		TextureHandle getNormalTexture() const { return normalTexture; }
		TextureHandle getMetallicTexture() const { return metallicTexture; }
		TextureHandle getRoughnessTexture() const { return roughnessTexture; }
		TextureHandle getAOTexture() const { return aoTexture; }
		TextureHandle getEmissiveTexture() const { return emissiveTexture; }
		TextureHandle getOpacityTexture() const { return opacityTexture; }
		TextureHandle getSpecularTexture() const { return specularTexture; }


		void setAlbedoTexture(TextureHandle t)
		{
			albedoTexture = t;
			incrementVersion();
		}
		void setNormalTexture(TextureHandle t)
		{
			normalTexture = t;
			incrementVersion();
		}
		void setMetallicTexture(TextureHandle t)
		{
			metallicTexture = t;
			incrementVersion();
		}
		void setRoughnessTexture(TextureHandle t)
		{
			roughnessTexture = t;
			incrementVersion();
		}
		void setAOTexture(TextureHandle t)
		{
			aoTexture = t;
			incrementVersion();
		}
		void setEmissiveTexture(TextureHandle t)
		{
			emissiveTexture = t;
			incrementVersion();
		}
		void setOpacityTexture(TextureHandle t)
		{
			opacityTexture = t;
			incrementVersion();
		}
		void setSpecularTexture(TextureHandle t)
		{
			specularTexture = t;
			incrementVersion();
		}

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
		MaterialProperties properties{};
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
