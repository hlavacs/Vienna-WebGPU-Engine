#pragma once

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
 * @class WebGPUBindGroupLayoutInfo
 * @brief GPU-side bind group layout: wraps a WebGPU bind group layout and its descriptor.
 *
 * This class encapsulates a WebGPU bind group layout and its associated descriptor,
 * providing accessors for all relevant properties and ensuring resource cleanup.
 * Used for managing bind group layouts throughout the rendering pipeline.
 *
 * Extended to support material slot name mapping for texture bindings and unique keys for global buffers.
 */
class WebGPUBindGroupLayoutInfo
{
  public:
	/**
	 * @brief Constructs a WebGPUBindGroupLayoutInfo from descriptor and GPU objects.
	 *
	 * @param layout The GPU-side bind group layout.
	 * @param layoutDesc The bind group layout descriptor used to create the layout.
	 *
	 * @throws Assertion failure if layout is invalid.
	 */
	WebGPUBindGroupLayoutInfo(
		wgpu::BindGroupLayout layout,
		const wgpu::BindGroupLayoutDescriptor &layoutDesc
	) : m_layout(layout),
		m_layoutDesc(layoutDesc)
	{
		assert(m_layout && "BindGroupLayout must be valid");
		assert(m_layoutDesc.entryCount > 0 && "BindGroupLayoutInfo must have at least one entry");

		// Copy entries to ensure they remain valid (descriptor entries pointer can be temporary)
		m_entries.assign(m_layoutDesc.entries, m_layoutDesc.entries + m_layoutDesc.entryCount);

		// Update descriptor to point to our owned copy
		m_layoutDesc.entries = m_entries.data();
	}

	/**
	 * @brief Sets a unique key for this bind group layout, used to retrieve global buffers.
	 * @param key Unique key (e.g., "frameUniforms", "lightUniforms")
	 */
	void setKey(const std::optional<std::string> &key) { m_key = key; }

	/**
	 * @brief Gets the unique key for this bind group layout.
	 * @return Optional unique key string.
	 */
	[[nodiscard]] const std::optional<std::string> &getKey() const { return m_key; }

	/**
	 * @brief Sets whether this bind group is global. Only relevant if a key is set.
	 * @param isGlobal True if global, false otherwise.
	 */
	void setGlobal(bool isGlobal) { m_isGlobal = isGlobal; }

	/**
	 * @brief Gets whether this bind group is global.
	 * @return True if global, false otherwise.
	 */
	[[nodiscard]] bool isGlobal() const { return m_isGlobal; }

	/**
	 * @brief Destructor that cleans up WebGPU resources.
	 */
	~WebGPUBindGroupLayoutInfo()
	{
		if (m_layout)
		{
			m_layout.release();
		}
	}

	/**
	 * @brief Gets the underlying WebGPU bind group layout.
	 * @return The WebGPU bind group layout object.
	 */
	[[nodiscard]] wgpu::BindGroupLayout getLayout() const { return m_layout; }

	/**
	 * @brief Gets the bind group layout descriptor.
	 * @return The WebGPU bind group layout descriptor.
	 */
	[[nodiscard]] const wgpu::BindGroupLayoutDescriptor &getLayoutDescriptor() const { return m_layoutDesc; }

	/**
	 * @brief Gets the number of entries in the bind group layout.
	 * @return Number of entries.
	 */
	[[nodiscard]] uint32_t getEntryCount() const { return m_layoutDesc.entryCount; }

	/**
	 * @brief Gets the entries of the bind group layout.
	 * @return Const reference to vector of bind group layout entries.
	 */
	[[nodiscard]] const std::vector<wgpu::BindGroupLayoutEntry> &getEntries() const
	{
		return m_entries;
	}

	/**
	 * @brief Gets a specific entry by index.
	 * @param index Index of the entry to retrieve.
	 * @return Pointer to the bind group layout entry at the specified index.
	 * @throws Assertion failure if index is out of bounds.
	 */
	[[nodiscard]] const wgpu::BindGroupLayoutEntry *getEntry(size_t index) const
	{
		assert(index < m_entries.size() && "Entry index out of bounds");
		return &m_entries[index];
	}

	/**
	 * @brief Finds an entry by binding number.
	 * @param binding The binding number to search for.
	 * @return Pointer to the entry if found, nullptr otherwise.
	 */
	[[nodiscard]] const wgpu::BindGroupLayoutEntry *findEntryByBinding(uint32_t binding) const
	{
		for (const auto &entry : m_entries)
		{
			if (entry.binding == binding)
			{
				return &entry;
			}
		}
		return nullptr;
	}

	/**
	 * @brief Sets the material slot name for a specific binding.
	 * @param binding The binding index.
	 * @param slotName The material slot name (e.g., "albedo", "normal").
	 */
	void setMaterialSlotName(uint32_t binding, const std::string &slotName)
	{
		if (!slotName.empty())
		{
			m_materialSlotNames[binding] = slotName;
		}
	}

	/**
	 * @brief Sets the fallback color for a specific binding.
	 * @param binding The binding index.
	 * @param color The fallback color as glm::vec3 (optional).
	 */
	void setFallbackColor(uint32_t binding, const std::optional<glm::vec3> &color)
	{
		if (color.has_value())
		{
			m_fallbackColors[binding] = color.value();
		}
	}

	/**
	 * @brief Gets the material slot name for a specific binding.
	 * @param binding The binding index.
	 * @return The material slot name, or empty string if not set.
	 */
	[[nodiscard]] std::string getMaterialSlotName(uint32_t binding) const
	{
		auto it = m_materialSlotNames.find(binding);
		return (it != m_materialSlotNames.end()) ? it->second : "";
	}

	/**
	 * @brief Gets the fallback color for a specific binding.
	 * @param binding The binding index.
	 * @return The fallback color as glm::vec3, or std::nullopt if not set.
	 */
	[[nodiscard]] std::optional<glm::vec3> getFallbackColor(uint32_t binding) const
	{
		auto it = m_fallbackColors.find(binding);
		if (it != m_fallbackColors.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

  private:
	/** Unique key for global bind group retrieval (frameUniforms, lightUniforms, objectUniforms, etc.) */
	std::optional<std::string> m_key;

	/** Underlying WebGPU bind group layout */
	wgpu::BindGroupLayout m_layout;

	/** Descriptor used to create the bind group layout */
	wgpu::BindGroupLayoutDescriptor m_layoutDesc;

	/** Owned copy of layout entries to ensure lifetime */
	std::vector<wgpu::BindGroupLayoutEntry> m_entries;

	/** Maps binding index to material slot name for texture bindings */
	std::unordered_map<uint32_t, std::string> m_materialSlotNames;

	/** Maps binding index to fallback color for texture bindings */
	std::unordered_map<uint32_t, glm::vec3> m_fallbackColors;

	/** Whether this bind group is global (only relevant if a key is set) */
	bool m_isGlobal = false;
};

} // namespace engine::rendering::webgpu
