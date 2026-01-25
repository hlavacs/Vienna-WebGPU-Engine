#pragma once

#include <cassert>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

/**
 * @brief Determines whether a bind group can be reused across shaders/objects.
 */
enum class BindGroupReuse
{
	Global,		 //< device-wide, never changes
	PerFrame,	 //< per camera or per frame
	PerMaterial, //< tied to material instance
	PerObject	 //< per render item / draw call
};

/**
 * @brief Semantic type of a bind group.
 */
enum class BindGroupType
{
	Frame,
	Light,
	Mipmap,
	Object,
	Material,
	Shadow,
	ShadowPass2D,
	ShadowPassCube,
	Debug,
	Custom,
};

/**
 * @brief Type of a single binding inside a bind group.
 */
enum class BindingType
{
	UniformBuffer,
	StorageBuffer,
	Texture,
	MaterialTexture,
	Sampler
};

/**
 * @brief Metadata describing a single binding in a bind group layout.
 */
struct BindGroupBinding // ToDo: Constructor that lets it build from wgpu::BindGroupLayoutEntry and extra info
{
	uint32_t bindingIndex;									///< GPU binding index
	std::string name;										///< Human-readable name for retrieval by slot name
	BindingType type;										///< Type of the binding
	wgpu::ShaderStage visibility = wgpu::ShaderStage::None; ///< Shader stages this binding is visible in

	// For buffers
	size_t size = 0;
	// Only for textures
	std::optional<std::string> materialSlotName; ///< Material slot name or debug name
	std::optional<glm::vec3> fallbackColor;		 ///< Default color if texture is missing
};

/**
 * @class WebGPUBindGroupLayoutInfo
 * @brief Encapsulates a GPU bind group layout and its typed bindings.
 *
 * Provides:
 * - Typed binding metadata (uniforms, textures, samplers)
 * - Global/shared reuse management
 * - Helper queries for passes and materials
 */
class WebGPUBindGroupLayoutInfo
{
  public:
	/**
	 * @brief Constructs a bind group layout info with typed bindings.
	 * @param layout GPU-side bind group layout handle
	 * @param layoutDesc Descriptor used to create the layout
	 * @param name Human-readable name for this bind group (also used as key for shared groups)
	 * @param type Semantic type of the bind group
	 * @param reuse Whether the bind group can be shared across shaders/objects
	 * @param bindings Typed bindings contained in this layout
	 */
	WebGPUBindGroupLayoutInfo(
		wgpu::BindGroupLayout layout,
		const wgpu::BindGroupLayoutDescriptor &layoutDesc,
		std::string name,
		BindGroupType type,
		BindGroupReuse reuse,
		std::vector<BindGroupBinding> bindings
	) : m_layout(layout),
		m_layoutDesc(layoutDesc),
		m_name(std::move(name)),
		m_type(type),
		m_reuse(reuse),
		m_bindings(std::move(bindings))
	{
		assert(m_layout && "BindGroupLayout must be valid");
		assert(!m_bindings.empty() && "BindGroupLayoutInfo must have at least one binding");

		// Copy entries to own memory to ensure descriptor lifetime
		m_entries.assign(m_layoutDesc.entries, m_layoutDesc.entries + m_layoutDesc.entryCount);
		m_layoutDesc.entries = m_entries.data();
		buildLookupTables();
	}

	~WebGPUBindGroupLayoutInfo()
	{
		if (m_layout)
			m_layout.release();
	}

	/**
	 * @brief Gets the name of the bind group layout.
	 * @note Used as key for reusable bind groups.
	 * @return Name string
	 */
	[[nodiscard]] const std::string &getName() const { return m_name; }
	/**
	 * @brief Gets the semantic type of the bind group.
	 * @note Used for identifying standard bind groups.
	 * @return BindGroupType enum value
	 */
	[[nodiscard]] BindGroupType getType() const { return m_type; }

	/**
	 * @brief Gets the underlying WebGPU bind group layout.
	 * @return The WebGPU bind group layout object
	 */
	[[nodiscard]] const wgpu::BindGroupLayout &getLayout() const { return m_layout; }
	/**
	 * @brief Gets the descriptor used to create the bind group layout.
	 * @return The bind group layout descriptor
	 */
	[[nodiscard]] const wgpu::BindGroupLayoutDescriptor &getLayoutDescriptor() const { return m_layoutDesc; }

	/**
	 * @brief Gets the typed bindings in this bind group layout.
	 * @return Vector of BindGroupBinding metadata
	 */
	[[nodiscard]] const std::vector<BindGroupBinding> &getBindings() const { return m_bindings; }

	/**
	 * @brief Gets the typed bindings in this bind group layout.
	 * @return Vector of BindGroupBinding metadata
	 */
	[[nodiscard]] const std::vector<wgpu::BindGroupLayoutEntry> &getEntries() const { return m_entries; }

	/**
	 * @brief Gets the reuse policy of the bind group.
	 * @return BindGroupReuse enum value
	 */
	[[nodiscard]] BindGroupReuse getReuse() const { return m_reuse; }

	/**
	 * @brief Get cache key for reusable bind groups
	 */
	[[nodiscard]] std::string getCacheKey() const { return m_name; }

	/**
	 * @brief Check if a binding exists by its index
	 * @param slotName Name of the binding slot
	 */
	[[nodiscard]] bool hasBinding(const std::string &slotName) const
	{
		return m_slotNameMap.find(slotName) != m_slotNameMap.end();
	}

	[[nodiscard]] const BindGroupBinding *getBinding(uint32_t bindingIndex) const
	{
		if(m_bindings.size() <= bindingIndex)
            return nullptr;
		return &m_bindings[bindingIndex];
	}

	[[nodiscard]] const BindGroupBinding *getBinding(const std::string &slotName) const
	{
		auto it = m_slotNameMap.find(slotName);
		if (it == m_slotNameMap.end())
			return nullptr;
		return &m_bindings[it->second];
	}

	[[nodiscard]] const BindGroupBinding *getBindingByMaterialSlot(const std::string &materialSlot) const
	{
		auto it = m_materialSlotNameMap.find(materialSlot);
		if (it == m_materialSlotNameMap.end())
			return nullptr;
		return &m_bindings[it->second];
	}

	[[nodiscard]] std::optional<size_t> getBindingIndex(const std::string &slotName) const
	{
		auto it = m_slotNameMap.find(slotName);
		if (it == m_slotNameMap.end())
			return std::nullopt;
		return it->second;
	}
	/**
	 * @brief Get layout entry by index
	 */
	[[nodiscard]] const wgpu::BindGroupLayoutEntry *getLayoutEntry(uint32_t bindingIndex) const
	{
		if(m_entries.size() <= bindingIndex)
            return nullptr;
		return &m_entries[bindingIndex];
	}

	[[nodiscard]] const wgpu::BindGroupLayoutEntry *getLayoutEntry(const std::string &slotName) const
	{
		auto it = m_slotNameMap.find(slotName);
		if (it == m_slotNameMap.end())
			return nullptr;
		return &m_entries[it->second];
	}

	/**
	 * @brief Get binding type by index
	 */
	[[nodiscard]] std::optional<BindingType> getBindingType(uint32_t bindingIndex) const
	{
		if (const auto *b = getBinding(bindingIndex))
			return b->type;
		return std::nullopt; // default fallback
	}

	/**
	 * @brief Get texture slot name (only valid for Texture bindings)
	 */
	[[nodiscard]] std::string getMaterialSlotName(uint32_t bindingIndex) const
	{
		if (const auto *b = getBinding(bindingIndex))
			if (b->type == BindingType::MaterialTexture)
				return b->materialSlotName.value_or("");
		return "";
	}

	/**
	 * @brief Get texture fallback color (only valid for Texture bindings)
	 */
	[[nodiscard]] std::optional<glm::vec3> getMaterialFallbackColor(std::string materialSlot) const
	{
		if (const auto *b = getBindingByMaterialSlot(materialSlot))
			if (b->type == BindingType::MaterialTexture)
				return b->fallbackColor;
		return std::nullopt;
	}

	/**
	 * @brief Get texture fallback color (only valid for Texture bindings)
	 * @param bindingIndex Binding index to query
	 * @return Optional glm::vec3 fallback color
	 */
	[[nodiscard]] std::optional<glm::vec3> getMaterialFallbackColor(uint32_t bindingIndex) const
	{
		if (const auto *b = getBinding(bindingIndex))
			if (b->type == BindingType::MaterialTexture)
				return b->fallbackColor;
		return std::nullopt;
	}

  private:
	void buildLookupTables()
	{
		for (size_t i = 0; i < m_entries.size(); ++i)
		{
			assert(
				m_entries[i].binding == m_bindings[i].bindingIndex && "BindGroupLayoutEntry and BindGroupBinding mismatch"
			);
		}
		for (size_t i = 0; i < m_bindings.size(); ++i)
		{
			const auto &b = m_bindings[i];

			m_slotNameMap.emplace(b.name, i);
			if (b.type == BindingType::MaterialTexture)
			{
				assert(
					!b.materialSlotName.value_or("").empty() && "Material texture bindings must have a slot name"
				);

				m_materialSlotNameMap.emplace(b.materialSlotName.value_or(""), i);
			}
		}
	}

	std::string m_name; ///< Name / optional key for reusable bindgroups
	BindGroupType m_type;
	BindGroupReuse m_reuse;

	wgpu::BindGroupLayout m_layout;
	wgpu::BindGroupLayoutDescriptor m_layoutDesc;
	std::vector<wgpu::BindGroupLayoutEntry> m_entries;

	std::vector<BindGroupBinding> m_bindings; ///< Typed binding metadata
	std::unordered_map<std::string, size_t> m_slotNameMap;
	std::unordered_map<std::string, size_t> m_materialSlotNameMap;
};

} // namespace engine::rendering::webgpu
