#pragma once

#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <variant>

#include <glm/glm.hpp>

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/rendering/ColorSpace.h"
#include "engine/rendering/MaterialFeatureMask.h"
#include "engine/rendering/ShaderRegistry.h"
#include "engine/rendering/Texture.h"
namespace engine::rendering
{
using TextureHandle = Texture::Handle;

/**
 * @brief Represents a texture slot with color space information.
 */
struct TextureSlot
{
	TextureHandle handle;
	ColorSpace colorSpace = ColorSpace::sRGB;

	TextureSlot() = default;
	TextureSlot(TextureHandle h, ColorSpace cs = ColorSpace::sRGB) : handle(h), colorSpace(cs) {}

	[[nodiscard]] bool isValid() const { return handle.valid(); }
};

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

inline constexpr const char *OCCLUSION = "occlusion"; // Occlusion map

} // namespace MaterialTextureSlots

/**
 * @brief Returns the default color space for a given texture slot.
 * @param slotName Name of the texture slot.
 * @return The default ColorSpace for that slot.
 */
inline ColorSpace defaultColorSpaceForSlot(const std::string &slotName)
{
	// sRGB textures (color data that should be gamma-corrected)
	if (slotName == MaterialTextureSlots::DIFFUSE || slotName == MaterialTextureSlots::EMISSIVE || slotName == MaterialTextureSlots::SPECULAR || slotName == MaterialTextureSlots::AMBIENT)
	{
		return ColorSpace::sRGB;
	}

	// Linear textures (non-color data)
	if (slotName == MaterialTextureSlots::NORMAL || slotName == MaterialTextureSlots::ROUGHNESS || slotName == MaterialTextureSlots::METALLIC || slotName == MaterialTextureSlots::OCCLUSION || slotName == MaterialTextureSlots::BUMP || slotName == MaterialTextureSlots::DISPLACEMENT || slotName == MaterialTextureSlots::ALPHA || slotName == MaterialTextureSlots::SPECULAR_HIGHLIGHT || slotName == MaterialTextureSlots::SHEEN || slotName == MaterialTextureSlots::REFLECTION)
	{
		return ColorSpace::Linear;
	}

	// Default to linear for unknown slots
	return ColorSpace::Linear;
}

struct PBRProperties
{
	float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // RGBA
	float emission[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float transmittance[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float ambient[4] = {1.0f, 1.0f, 1.0f, 1.0f};

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

struct MaterialPropertiesData
{
	std::shared_ptr<void> data; // type-erased storage
	size_t size = 0;			// size in bytes
	std::type_index type;		// type info

	template <typename T>
	static MaterialPropertiesData create(const T &value)
	{
		auto p = std::make_shared<T>(value);
		return MaterialPropertiesData{p, sizeof(T), std::type_index(typeid(T))};
	}

	template <typename T>
	bool isType() const
	{
		return type == std::type_index(typeid(T));
	}

	template <typename T>
	T &as()
	{
		if (type != std::type_index(typeid(T)))
			throw std::runtime_error("Property type mismatch");
		return *reinterpret_cast<T *>(data.get());
	}

	template <typename T>
	const T &as() const
	{
		if (type != std::type_index(typeid(T)))
			throw std::runtime_error("Property type mismatch");
		return *reinterpret_cast<const T *>(data.get());
	}

	/**
	 * @brief Returns a raw pointer and size to the stored data.
	 */
	const void *getData() const
	{
		return data.get();
	}

	/**
	 * @brief Non-const version (for updating GPU buffers)
	 */
	void *getData()
	{
		return data.get();
	}
};

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

	template <typename T>
	void setProperties(const T &props)
	{
		m_properties = MaterialPropertiesData::create(props);
		incrementVersion();
	}

	template <typename T>
	const T &getProperties() const
	{
		return m_properties.as<T>();
	}

	const void *getPropertiesData() const
	{
		return m_properties.getData();
	}

	size_t getPropertiesSize() const
	{
		return m_properties.size;
	}

	std::type_index getPropertiesType() const
	{
		return m_properties.type;
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
	 * @param colorSpace Color space for the texture. Defaults to Linear unless specified otherwise.
	 */
	void setTexture(const std::string &slotName, TextureHandle texture, ColorSpace colorSpace = ColorSpace::Linear)
	{
		if (texture.valid())
		{
			textures[slotName] = TextureSlot(texture, colorSpace);
		}
		else
			textures.erase(slotName);
		incrementVersion();
	}

	/**
	 * @brief Get a texture slot by name.
	 * @param slotName Name of the texture slot.
	 * @return TextureSlot, or invalid slot if not found.
	 */
	TextureSlot getTextureSlot(const std::string &slotName) const
	{
		auto it = textures.find(slotName);
		return it != textures.end() ? it->second : TextureSlot{};
	}

	/**
	 * @brief Get a texture handle by slot name.
	 * @param slotName Name of the texture slot.
	 * @return Texture handle, or invalid handle if not found.
	 */
	TextureHandle getTexture(const std::string &slotName) const
	{
		auto it = textures.find(slotName);
		return it != textures.end() ? it->second.handle : TextureHandle{};
	}

	/**
	 * @brief Get the color space for a texture slot.
	 * @param slotName Name of the texture slot.
	 * @return ColorSpace, or Linear if not found.
	 */
	ColorSpace getColorSpace(const std::string &slotName) const
	{
		auto it = textures.find(slotName);
		return it != textures.end() ? it->second.colorSpace : ColorSpace::Linear;
	}

	/**
	 * @brief Check if a texture slot has a valid texture.
	 * @param slotName Name of the texture slot.
	 * @return True if texture exists and is valid.
	 */
	bool hasTexture(const std::string &slotName) const
	{
		auto it = textures.find(slotName);
		return it != textures.end() && it->second.isValid();
	}

	/**
	 * @brief Get all texture slots.
	 * @return Map of texture slot names to texture slots.
	 */
	const std::unordered_map<std::string, TextureSlot> &getTextureSlots() const { return textures; }

	/**
	 * @brief Get all textures (legacy method for backward compatibility).
	 * @return Map of texture slot names to texture handles.
	 */
	std::unordered_map<std::string, TextureHandle> getTextures() const
	{
		std::unordered_map<std::string, TextureHandle> result;
		for (const auto &[slot, texSlot] : textures)
		{
			result[slot] = texSlot.handle;
		}
		return result;
	}

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

	TextureSlot getAmbientTextureSlot() const { return getTextureSlot(MaterialTextureSlots::AMBIENT); }
	TextureSlot getDiffuseTextureSlot() const { return getTextureSlot(MaterialTextureSlots::DIFFUSE); }
	TextureSlot getSpecularTextureSlot() const { return getTextureSlot(MaterialTextureSlots::SPECULAR); }
	TextureSlot getSpecularHighlightTextureSlot() const { return getTextureSlot(MaterialTextureSlots::SPECULAR_HIGHLIGHT); }
	TextureSlot getBumpTextureSlot() const { return getTextureSlot(MaterialTextureSlots::BUMP); }
	TextureSlot getDisplacementTextureSlot() const { return getTextureSlot(MaterialTextureSlots::DISPLACEMENT); }
	TextureSlot getReflectionTextureSlot() const { return getTextureSlot(MaterialTextureSlots::REFLECTION); }
	TextureSlot getAlphaTextureSlot() const { return getTextureSlot(MaterialTextureSlots::ALPHA); }
	TextureSlot getRoughnessTextureSlot() const { return getTextureSlot(MaterialTextureSlots::ROUGHNESS); }
	TextureSlot getMetallicTextureSlot() const { return getTextureSlot(MaterialTextureSlots::METALLIC); }
	TextureSlot getSheenTextureSlot() const { return getTextureSlot(MaterialTextureSlots::SHEEN); }
	TextureSlot getEmissiveTextureSlot() const { return getTextureSlot(MaterialTextureSlots::EMISSIVE); }
	TextureSlot getNormalTextureSlot() const { return getTextureSlot(MaterialTextureSlots::NORMAL); }
	TextureSlot getOcclusionTextureSlot() const { return getTextureSlot(MaterialTextureSlots::OCCLUSION); }

	void setAmbientTexture(TextureHandle t) { setTexture(MaterialTextureSlots::AMBIENT, t, defaultColorSpaceForSlot(MaterialTextureSlots::AMBIENT)); }
	void setDiffuseTexture(TextureHandle t) { setTexture(MaterialTextureSlots::DIFFUSE, t, defaultColorSpaceForSlot(MaterialTextureSlots::DIFFUSE)); }
	void setSpecularTexture(TextureHandle t) { setTexture(MaterialTextureSlots::SPECULAR, t, defaultColorSpaceForSlot(MaterialTextureSlots::SPECULAR)); }
	void setSpecularHighlightTexture(TextureHandle t) { setTexture(MaterialTextureSlots::SPECULAR_HIGHLIGHT, t, defaultColorSpaceForSlot(MaterialTextureSlots::SPECULAR_HIGHLIGHT)); }
	void setBumpTexture(TextureHandle t) { setTexture(MaterialTextureSlots::BUMP, t, defaultColorSpaceForSlot(MaterialTextureSlots::BUMP)); }
	void setDisplacementTexture(TextureHandle t) { setTexture(MaterialTextureSlots::DISPLACEMENT, t, defaultColorSpaceForSlot(MaterialTextureSlots::DISPLACEMENT)); }
	void setReflectionTexture(TextureHandle t) { setTexture(MaterialTextureSlots::REFLECTION, t, defaultColorSpaceForSlot(MaterialTextureSlots::REFLECTION)); }
	void setAlphaTexture(TextureHandle t) { setTexture(MaterialTextureSlots::ALPHA, t, defaultColorSpaceForSlot(MaterialTextureSlots::ALPHA)); }
	void setRoughnessTexture(TextureHandle t) { setTexture(MaterialTextureSlots::ROUGHNESS, t, defaultColorSpaceForSlot(MaterialTextureSlots::ROUGHNESS)); }
	void setMetallicTexture(TextureHandle t) { setTexture(MaterialTextureSlots::METALLIC, t, defaultColorSpaceForSlot(MaterialTextureSlots::METALLIC)); }
	void setSheenTexture(TextureHandle t) { setTexture(MaterialTextureSlots::SHEEN, t, defaultColorSpaceForSlot(MaterialTextureSlots::SHEEN)); }
	void setEmissiveTexture(TextureHandle t) { setTexture(MaterialTextureSlots::EMISSIVE, t, defaultColorSpaceForSlot(MaterialTextureSlots::EMISSIVE)); }
	void setNormalTexture(TextureHandle t) { setTexture(MaterialTextureSlots::NORMAL, t, defaultColorSpaceForSlot(MaterialTextureSlots::NORMAL)); }
	void setOcclusionTexture(TextureHandle t) { setTexture(MaterialTextureSlots::OCCLUSION, t, defaultColorSpaceForSlot(MaterialTextureSlots::OCCLUSION)); }

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
	MaterialPropertiesData m_properties{MaterialPropertiesData::create(PBRProperties{})};

	/**
	 * @brief The identifier of the shader used by this material.
	 */
	std::string m_shader;

	/**
	 * @brief Material feature mask.
	 */
	MaterialFeature::Flag m_featureMask = MaterialFeature::Flag::None;

	/**
	 * @brief Texture dictionary mapping slot names to texture slots.
	 */
	std::unordered_map<std::string, TextureSlot> textures;
};

} // namespace engine::rendering
