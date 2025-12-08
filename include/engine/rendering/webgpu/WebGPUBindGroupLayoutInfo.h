#pragma once

#include <memory>
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
 * Extended to support lazy buffer creation with isGlobal flag and material slot name mapping.
 */
class WebGPUBindGroupLayoutInfo
{
  public:
	/**
	 * @brief Constructs a WebGPUBindGroupLayoutInfo from descriptor and GPU objects.
	 *
	 * @param layout The GPU-side bind group layout.
	 * @param layoutDesc The bind group layout descriptor used to create the layout.
	 * @param isGlobal Whether buffers in this bind group are engine-managed (shared/cached) or per-material.
	 *
	 * @throws Assertion failure if layout is invalid.
	 */
	WebGPUBindGroupLayoutInfo(
		wgpu::BindGroupLayout layout,
		const wgpu::BindGroupLayoutDescriptor &layoutDesc,
		bool isGlobal = false
	) : m_layout(layout),
		m_layoutDesc(layoutDesc),
		m_isGlobal(isGlobal)
	{
		assert(m_layout && "BindGroupLayout must be valid");
		assert(m_layoutDesc.entryCount > 0 && "BindGroupLayoutInfo must have at least one entry");

		// Copy entries to ensure they remain valid (descriptor entries pointer can be temporary)
		m_entries.assign(m_layoutDesc.entries, m_layoutDesc.entries + m_layoutDesc.entryCount);

		// Update descriptor to point to our owned copy
		m_layoutDesc.entries = m_entries.data();
	}

	/**
	 * @brief Destructor that cleans up WebGPU resources.
	 *
	 * Releases the bind group layout to prevent memory leaks.
	 */
	~WebGPUBindGroupLayoutInfo()
	{
		if (m_layout)
		{
			m_layout.release();
		}
	}

	/**
	 * @brief Checks if the layout matches the given entry count.
	 * @param entryCount Number of entries to check.
	 * @return True if entry count matches, false otherwise.
	 */
	bool matches(uint32_t entryCount) const
	{
		return getEntryCount() == entryCount;
	}

	/**
	 * @brief Gets the underlying WebGPU bind group layout.
	 * @return The WebGPU bind group layout object.
	 */
	wgpu::BindGroupLayout getLayout() const { return m_layout; }

	/**
	 * @brief Gets the bind group layout descriptor.
	 * @return The WebGPU bind group layout descriptor.
	 */
	const wgpu::BindGroupLayoutDescriptor &getLayoutDescriptor() const { return m_layoutDesc; }

	/**
	 * @brief Gets the number of entries in the bind group layout.
	 * @return Number of entries.
	 */
	uint32_t getEntryCount() const { return m_layoutDesc.entryCount; }

	/**
	 * @brief Gets the entries of the bind group layout.
	 * @return Const reference to vector of bind group layout entries.
	 */
	const std::vector<wgpu::BindGroupLayoutEntry> &getEntries() const
	{
		return m_entries;
	}

	/**
	 * @brief Gets a specific entry by index.
	 * @param index Index of the entry to retrieve.
	 * @return Pointer to the bind group layout entry at the specified index.
	 * @throws Assertion failure if index is out of bounds.
	 */
	const wgpu::BindGroupLayoutEntry *getEntry(size_t index) const
	{
		assert(index < m_entries.size() && "Entry index out of bounds");
		return &m_entries[index];
	}

	/**
	 * @brief Finds an entry by binding number.
	 * @param binding The binding number to search for.
	 * @return Pointer to the entry if found, nullptr otherwise.
	 */
	const wgpu::BindGroupLayoutEntry *findEntryByBinding(uint32_t binding) const
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
	 * @brief Checks if buffers in this bind group are engine-managed (global/cached).
	 * @return True if global (shared/cached buffers), false if per-material.
	 */
	bool isGlobal() const { return m_isGlobal; }

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
	 * @brief Gets the material slot name for a specific binding.
	 * @param binding The binding index.
	 * @return The material slot name, or empty string if not set.
	 */
	std::string getMaterialSlotName(uint32_t binding) const
	{
		auto it = m_materialSlotNames.find(binding);
		return (it != m_materialSlotNames.end()) ? it->second : "";
	}

  protected:
	/**
	 * @brief The underlying WebGPU bind group layout resource.
	 */
	wgpu::BindGroupLayout m_layout;

	/**
	 * @brief Descriptor used to create the bind group layout.
	 */
	wgpu::BindGroupLayoutDescriptor m_layoutDesc;

	/**
	 * @brief Owned copy of layout entries to ensure lifetime.
	 */
	std::vector<wgpu::BindGroupLayoutEntry> m_entries;

	/**
	 * @brief Whether buffers in this bind group are engine-managed (global/cached) or per-material.
	 */
	bool m_isGlobal = false;

	/**
	 * @brief Maps binding index to material slot name for texture bindings.
	 * Used to map shader bindings to material texture dictionary keys.
	 */
	std::unordered_map<uint32_t, std::string> m_materialSlotNames;
};

} // namespace engine::rendering::webgpu