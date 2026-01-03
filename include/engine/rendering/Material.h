#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/rendering/MaterialFeatureMask.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/Texture.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace engine::rendering
{
using TextureHandle = Texture::Handle;

/**
 * @brief Standard material texture slot names.
 * These correspond to common PBR material texture types and OBJ material properties.
 */
namespace MaterialTextureSlots
{
inline constexpr const char *AMBIENT = "ambient";						// Ambient occlusion
inline constexpr const char *DIFFUSE = "diffuse";						// Base color / diffuse
inline constexpr const char *SPECULAR = "specular";						// Specular map
inline constexpr const char *SPECULAR_HIGHLIGHT = "specular_highlight"; // Specular highlight map
inline constexpr const char *BUMP = "bump";								// Bump map
inline constexpr const char *DISPLACEMENT = "displacement";				// Displacement map
inline constexpr const char *ALPHA = "alpha";							// Alpha/opacity map
inline constexpr const char *REFLECTION = "reflection";					// Reflection map

inline constexpr const char *ROUGHNESS = "roughness"; // Roughness map
inline constexpr const char *METALLIC = "metallic";	  // Metalness map
inline constexpr const char *SHEEN = "sheen";		  // Sheen map
inline constexpr const char *EMISSIVE = "emissive";	  // Emissive/glow map
inline constexpr const char *NORMAL = "normal";		  // Normal map

inline constexpr const char *OCCLUSION = "occlusion";  // Occlusion map

} // namespace MaterialTextureSlots

struct PBRProperties
{
	float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // RGBA
	float emission[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float transmittance[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float ambient[4] = {0.0f, 0.0f, 0.0f, 0.0f};

	float roughness = 0.5f;
	float metallic = 0.0f;
	float ior = 1.5f;
	float normalTextureScale = 1.0f; 
};
static_assert(sizeof(PBRProperties) % 16 == 0, "PBRProperties must be 16-byte aligned");

struct UnlitProperties
{
	glm::vec4 color{1.f, 1.f, 1.f, 1.f}; // rgb + opacity
};
static_assert(sizeof(UnlitProperties) % 16 == 0);

using MaterialProperties = std::variant<
	PBRProperties,
	UnlitProperties>;

struct Material : public engine::core::Identifiable<Material>,
				  public engine::core::Versioned
{
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
	const MaterialProperties &getProperties() const { return m_properties; }

	/**
	 * @brief Get the size of the current properties struct.
	 * @return Size in bytes.
	 */
	const size_t getPropertiesSize() const
	{
		return std::visit(
			[](auto &&arg) -> size_t
			{
				return sizeof(arg);
			},
			m_properties
		);
	}

	/**
	 * @brief Set the material properties.
	 * @param properties The material properties struct.
	 * @tparam T Type of the properties struct (e.g., PBRProperties or UnlitProperties).
	 */
	template <typename T>
	void setProperties(const T &properties)
	{
		static_assert(
			std::is_same_v<T, PBRProperties> || std::is_same_v<T, UnlitProperties>,
			"Invalid material property type"
		);

		m_properties = properties;
		incrementVersion();
	}

	/**
	 * @brief Check if the material properties are of a specific type.
	 * @tparam T Type to check against (e.g., PBRProperties or UnlitProperties).
	 * @return True if the properties are of type T, false otherwise.
	 */
	template <typename T>
	bool isPropertiesKind() const
	{
		return std::holds_alternative<T>(m_properties);
	}

	template <typename T>
	const T &getProperties() const
	{
		return std::get<T>(m_properties);
	}

	/**
	 * @brief Get the shader identifier string.
	 * @return The shader identifier.
	 */
	const std::string &getShader() const { return m_shader; }

	/**
	 * @brief Set the shader for this material.
	 * @param shader Shader identifier string.
	 */
	void setShader(std::string shader)
	{
		m_shader = shader;
		incrementVersion();
	}

	/**
	 * @brief Get the material feature mask.
	 * @return The feature mask as a MaterialFeature.
	 */
	MaterialFeature::Flag getFeatureMask() const { return m_featureMask; }

	/**
	 * @brief Set the material feature mask.
	 * @param featureMask The feature mask as a MaterialFeature.
	 */
	void setFeatureMask(MaterialFeature::Flag featureMask)
	{
		m_featureMask = featureMask;
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

	// === Convenience Getters/Setters for Common Texture Slots ===

	TextureHandle getAmbientTexture() const { return getTexture(MaterialTextureSlots::AMBIENT); }
	TextureHandle getDiffuseTexture() const { return getTexture(MaterialTextureSlots::DIFFUSE); }
	TextureHandle getSpecularTexture() const { return getTexture(MaterialTextureSlots::SPECULAR); }
	TextureHandle getSpecularHighlightTexture() const { return getTexture(MaterialTextureSlots::SPECULAR_HIGHLIGHT); }
	TextureHandle getBumpTexture() const { return getTexture(MaterialTextureSlots::BUMP); }
	TextureHandle getDisplacementTexture() const { return getTexture(MaterialTextureSlots::DISPLACEMENT); }
	TextureHandle getReflectionTexture() const { return getTexture(MaterialTextureSlots::REFLECTION); }
	TextureHandle getAlphaTexture() const { return getTexture(MaterialTextureSlots::ALPHA); }
	TextureHandle getRoughnessTexture() const { return getTexture(MaterialTextureSlots::ROUGHNESS); }
	TextureHandle getMetallicTexture() const { return getTexture(MaterialTextureSlots::METALLIC); }
	TextureHandle getSheenTexture() const { return getTexture(MaterialTextureSlots::SHEEN); }
	TextureHandle getEmissiveTexture() const { return getTexture(MaterialTextureSlots::EMISSIVE); }
	TextureHandle getNormalTexture() const { return getTexture(MaterialTextureSlots::NORMAL); }
	TextureHandle getOcclusionTexture() const { return getTexture(MaterialTextureSlots::OCCLUSION); }

	void setAmbientTexture(TextureHandle t) { setTexture(MaterialTextureSlots::AMBIENT, t); }
	void setDiffuseTexture(TextureHandle t) { setTexture(MaterialTextureSlots::DIFFUSE, t); }
	void setSpecularTexture(TextureHandle t) { setTexture(MaterialTextureSlots::SPECULAR, t); }
	void setSpecularHighlightTexture(TextureHandle t) { setTexture(MaterialTextureSlots::SPECULAR_HIGHLIGHT, t); }
	void setBumpTexture(TextureHandle t) { setTexture(MaterialTextureSlots::BUMP, t); }
	void setDisplacementTexture(TextureHandle t) { setTexture(MaterialTextureSlots::DISPLACEMENT, t); }
	void setReflectionTexture(TextureHandle t) { setTexture(MaterialTextureSlots::REFLECTION, t); }
	void setAlphaTexture(TextureHandle t) { setTexture(MaterialTextureSlots::ALPHA, t); }
	void setRoughnessTexture(TextureHandle t) { setTexture(MaterialTextureSlots::ROUGHNESS, t); }
	void setMetallicTexture(TextureHandle t) { setTexture(MaterialTextureSlots::METALLIC, t); }
	void setSheenTexture(TextureHandle t) { setTexture(MaterialTextureSlots::SHEEN, t); }
	void setEmissiveTexture(TextureHandle t) { setTexture(MaterialTextureSlots::EMISSIVE, t); }
	void setNormalTexture(TextureHandle t) { setTexture(MaterialTextureSlots::NORMAL, t); }
	void setOcclusionTexture(TextureHandle t) { setTexture(MaterialTextureSlots::OCCLUSION, t); }

	bool hasAmbientTexture() const { return hasTexture(MaterialTextureSlots::AMBIENT); }
	bool hasDiffuseTexture() const { return hasTexture(MaterialTextureSlots::DIFFUSE); }
	bool hasSpecularTexture() const { return hasTexture(MaterialTextureSlots::SPECULAR); }
	bool hasSpecularHighlightTexture() const { return hasTexture(MaterialTextureSlots::SPECULAR_HIGHLIGHT); }
	bool hasBumpTexture() const { return hasTexture(MaterialTextureSlots::BUMP); }
	bool hasDisplacementTexture() const { return hasTexture(MaterialTextureSlots::DISPLACEMENT); }
	bool hasReflectionTexture() const { return hasTexture(MaterialTextureSlots::REFLECTION); }
	bool hasAlphaTexture() const { return hasTexture(MaterialTextureSlots::ALPHA); }
	bool hasRoughnessTexture() const { return hasTexture(MaterialTextureSlots::ROUGHNESS); }
	bool hasMetallicTexture() const { return hasTexture(MaterialTextureSlots::METALLIC); }
	bool hasSheenTexture() const { return hasTexture(MaterialTextureSlots::SHEEN); }
	bool hasEmissiveTexture() const { return hasTexture(MaterialTextureSlots::EMISSIVE); }
	bool hasNormalTexture() const { return hasTexture(MaterialTextureSlots::NORMAL); }
	bool hasOcclusionTexture() const { return hasTexture(MaterialTextureSlots::OCCLUSION); }

  private:
	/**
	 * @brief Material properties stored in a struct.
	 */
	MaterialProperties m_properties{PBRProperties{}};

	/**
	 * @brief The identifier of the shader used by this material.
	 */
	std::string m_shader;

	/**
	 * @brief Material feature mask.
	 */
	MaterialFeature::Flag m_featureMask = MaterialFeature::Flag::None;

	/**
	 * @brief Texture dictionary mapping slot names to texture handles.
	 */
	std::unordered_map<std::string, TextureHandle> textures;
};

} // namespace engine::rendering
