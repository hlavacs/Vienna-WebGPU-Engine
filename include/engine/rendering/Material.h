#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/Texture.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace engine::rendering
{
using TextureHandle = Texture::Handle;

/**
 * @brief Standard material texture slot names.
 * These correspond to common PBR material texture types and OBJ material properties.
 */
namespace MaterialTextureSlots
{
inline constexpr const char *ALBEDO = "albedo";			// Base color / diffuse
inline constexpr const char *NORMAL = "normal";			// Normal map
inline constexpr const char *METALLIC = "metallic";		// Metalness map
inline constexpr const char *ROUGHNESS = "roughness";	// Roughness map
inline constexpr const char *AO = "ao";					// Ambient occlusion
inline constexpr const char *EMISSIVE = "emissive";		// Emissive/glow map
inline constexpr const char *OPACITY = "opacity";		// Alpha/opacity map
inline constexpr const char *SPECULAR = "specular";		// Specular map
inline constexpr const char *HEIGHT = "height";			// Height/displacement map
inline constexpr const char *REFLECTION = "reflection"; // Reflection map
} // namespace MaterialTextureSlots

struct Material : public engine::core::Identifiable<Material>,
				  public engine::core::Versioned
{

	struct MaterialProperties
	{
		float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // RGBA
		float specular[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // RGB + Specular intensity in A
		float emission[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		float transmittance[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		float ambient[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		float shininess = 32.0f;
		float roughness = 0.5f;
		float metallic = 0.0f;
		float ior = 1.5f;
	};
	static_assert(sizeof(MaterialProperties) % 16 == 0, "MaterialProperties must be 16-byte aligned");

	using Handle = engine::core::Handle<Material>;
	using Ptr = std::shared_ptr<Material>;

	// Constructors
	Material() = default;

	// Move only
	Material(Material &&) noexcept = default;
	Material &operator=(Material &&) noexcept = default;

	// No copy
	Material(const Material &) = delete;
	Material &operator=(const Material &) = delete;

	/**
	 * @brief Get the material properties.
	 * @return The material properties struct.
	 */
	const MaterialProperties &getProperties() const { return properties; }

	/**
	 * @brief Set the material properties.
	 * @param p The new material properties.
	 */
	void setProperties(const MaterialProperties &p)
	{
		properties = p;
		incrementVersion();
	}

	/**
	 * @brief Get the shader type used by this material.
	 * @return ShaderType enum value.
	 */
	ShaderType getShaderType() const { return shaderType; }

	/**
	 * @brief Set the shader type for this material.
	 * @param type ShaderType enum value.
	 */
	void setShaderType(ShaderType type)
	{
		shaderType = type;
		incrementVersion();
	}

	/**
	 * @brief Get the custom shader name (only valid if shaderType == ShaderType::Custom).
	 */
	const std::string &getCustomShaderName() const { return customShaderName; }

	/**
	 * @brief Set the custom shader name and switch to Custom shader type.
	 * @param name Name of the custom shader.
	 */
	void setCustomShaderName(const std::string &name)
	{
		customShaderName = name;
		shaderType = ShaderType::Custom;
		incrementVersion();
	}

	// === Texture Dictionary API ===

	/**
	 * @brief Set a texture by slot name.
	 * @param slotName Name of the texture slot (e.g., MaterialTextureSlots::ALBEDO).
	 * @param texture Handle to the texture.
	 */
	void setTexture(const std::string &slotName, TextureHandle texture)
	{
		if (texture.valid())
			textures[slotName] = texture;
		else
			textures.erase(slotName);
		incrementVersion();
	}

	/**
	 * @brief Get a texture by slot name.
	 * @param slotName Name of the texture slot.
	 * @return Texture handle, or invalid handle if not found.
	 */
	TextureHandle getTexture(const std::string &slotName) const
	{
		auto it = textures.find(slotName);
		return it != textures.end() ? it->second : TextureHandle{};
	}

	/**
	 * @brief Check if a texture slot has a valid texture.
	 * @param slotName Name of the texture slot.
	 * @return True if texture exists and is valid.
	 */
	bool hasTexture(const std::string &slotName) const
	{
		auto it = textures.find(slotName);
		return it != textures.end() && it->second.valid();
	}

	/**
	 * @brief Get all textures.
	 * @return Map of texture slot names to texture handles.
	 */
	const std::unordered_map<std::string, TextureHandle> &getTextures() const { return textures; }

	/**
	 * @brief Remove a texture from a slot.
	 * @param slotName Name of the texture slot to clear.
	 */
	void removeTexture(const std::string &slotName)
	{
		textures.erase(slotName);
		incrementVersion();
	}

	// === Legacy API (for backward compatibility) ===
	// These use the dictionary internally with standard slot names
	// ToDo: Get rid of this legacy API in the future
	TextureHandle getAlbedoTexture() const { return getTexture(MaterialTextureSlots::ALBEDO); }
	TextureHandle getNormalTexture() const { return getTexture(MaterialTextureSlots::NORMAL); }
	TextureHandle getMetallicTexture() const { return getTexture(MaterialTextureSlots::METALLIC); }
	TextureHandle getRoughnessTexture() const { return getTexture(MaterialTextureSlots::ROUGHNESS); }
	TextureHandle getAOTexture() const { return getTexture(MaterialTextureSlots::AO); }
	TextureHandle getEmissiveTexture() const { return getTexture(MaterialTextureSlots::EMISSIVE); }
	TextureHandle getOpacityTexture() const { return getTexture(MaterialTextureSlots::OPACITY); }
	TextureHandle getSpecularTexture() const { return getTexture(MaterialTextureSlots::SPECULAR); }

	void setAlbedoTexture(TextureHandle t) { setTexture(MaterialTextureSlots::ALBEDO, t); }
	void setNormalTexture(TextureHandle t) { setTexture(MaterialTextureSlots::NORMAL, t); }
	void setMetallicTexture(TextureHandle t) { setTexture(MaterialTextureSlots::METALLIC, t); }
	void setRoughnessTexture(TextureHandle t) { setTexture(MaterialTextureSlots::ROUGHNESS, t); }
	void setAOTexture(TextureHandle t) { setTexture(MaterialTextureSlots::AO, t); }
	void setEmissiveTexture(TextureHandle t) { setTexture(MaterialTextureSlots::EMISSIVE, t); }
	void setOpacityTexture(TextureHandle t) { setTexture(MaterialTextureSlots::OPACITY, t); }
	void setSpecularTexture(TextureHandle t) { setTexture(MaterialTextureSlots::SPECULAR, t); }

	bool hasAlbedoTexture() const { return hasTexture(MaterialTextureSlots::ALBEDO); }
	bool hasNormalTexture() const { return hasTexture(MaterialTextureSlots::NORMAL); }
	bool hasMetallicTexture() const { return hasTexture(MaterialTextureSlots::METALLIC); }
	bool hasRoughnessTexture() const { return hasTexture(MaterialTextureSlots::ROUGHNESS); }
	bool hasAOTexture() const { return hasTexture(MaterialTextureSlots::AO); }
	bool hasEmissiveTexture() const { return hasTexture(MaterialTextureSlots::EMISSIVE); }
	bool hasOpacityTexture() const { return hasTexture(MaterialTextureSlots::OPACITY); }
	bool hasSpecularTexture() const { return hasTexture(MaterialTextureSlots::SPECULAR); }

  private:
	/**
	 * @brief Material properties stored in a struct.
	 */
	MaterialProperties properties{};

	/**
	 * @brief The shader type used by this material.
	 */
	ShaderType shaderType = ShaderType::Lit;

	/**
	 * @brief Custom shader name. Only used if shaderType == ShaderType::Custom.
	 */
	std::string customShaderName;

	/**
	 * @brief Texture dictionary mapping slot names to texture handles.
	 */
	std::unordered_map<std::string, TextureHandle> textures;
};

} // namespace engine::rendering
