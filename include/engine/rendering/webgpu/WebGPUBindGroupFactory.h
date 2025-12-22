#pragma once

#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"

namespace engine::rendering::webgpu
{

class WebGPUContext;

/**
 * @brief Factory for creating and managing WebGPU bind groups and layouts.
 *
 * This class provides utility functions for creating bind group layouts, bind groups,
 * and their descriptors, as well as helpers for common layout entry patterns.
 * It also manages the lifetime of created bind groups and layouts.
 */
class WebGPUBindGroupFactory
{
  public:
	/**
	 * @brief Construct a new WebGPUBindGroupFactory.
	 * @param context Reference to the WebGPUContext.
	 */
	explicit WebGPUBindGroupFactory(WebGPUContext &context);

	/**
	 * @brief Destructor. Cleans up created bind groups and layouts.
	 */
	~WebGPUBindGroupFactory();

	/**
	 * @brief Helper to create a uniform buffer BindGroupLayoutEntry for type T.
	 * @tparam T Uniform struct type.
	 * @param binding Binding index (default: -1 for auto-assignment).
	 * @param visibility wgpu::ShaderStage stage visibility (default: Vertex | Fragment).
	 * @return wgpu::BindGroupLayoutEntry
	 */
	template <typename T>
	wgpu::BindGroupLayoutEntry createUniformBindGroupLayoutEntry(
		int binding = -1,
		uint32_t visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment
	)
	{
		static_assert(std::is_standard_layout_v<T>, "Uniform buffer type must be standard layout");
		wgpu::BindGroupLayoutEntry entry = {};
		entry.binding = static_cast<uint32_t>(binding); // will be replaced later if -1
		entry.visibility = visibility;
		entry.buffer.type = wgpu::BufferBindingType::Uniform;
		entry.buffer.hasDynamicOffset = false;
		entry.buffer.minBindingSize = sizeof(T);
		return entry;
	}

	/**
	 * @brief Helper to create a storage buffer BindGroupLayoutEntry.
	 * @param binding Binding index (default: -1 for auto-assignment).
	 * @param visibility wgpu::ShaderStage stage visibility (default: Vertex | Fragment).
	 * @param readOnly Whether the storage buffer is read-only (default: true).
	 * @return wgpu::BindGroupLayoutEntry
	 */
	wgpu::BindGroupLayoutEntry createStorageBindGroupLayoutEntry(
		int binding = -1,
		uint32_t visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
		bool readOnly = true
	);

	/**
	 * @brief Helper to create a sampler BindGroupLayoutEntry.
	 * @param binding Binding index (default: -1 for auto-assignment).
	 * @param visibility wgpu::ShaderStage stage visibility (default: Fragment).
	 * @param samplerType Type of sampler binding (default: Filtering).
	 * @return wgpu::BindGroupLayoutEntry
	 */
	wgpu::BindGroupLayoutEntry createSamplerBindGroupLayoutEntry(
		int binding = -1,
		uint32_t visibility = wgpu::ShaderStage::Fragment,
		wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering
	);

	/**
	 * @brief Helper to create a texture BindGroupLayoutEntry.
	 * @param binding Binding index (default: -1 for auto-assignment).
	 * @param visibility wgpu::ShaderStage stage visibility (default: Fragment).
	 * @param sampleType Type of texture sample (default: Float).
	 * @param viewDimension Texture view dimension (default: 2D).
	 * @param multisampled Whether the texture is multisampled (default: false).
	 * @return wgpu::BindGroupLayoutEntry
	 */
	wgpu::BindGroupLayoutEntry createTextureBindGroupLayoutEntry(
		int binding = -1,
		uint32_t visibility = wgpu::ShaderStage::Fragment,
		wgpu::TextureSampleType sampleType = wgpu::TextureSampleType::Float,
		wgpu::TextureViewDimension viewDimension = wgpu::TextureViewDimension::_2D,
		bool multisampled = false
	);

	/**
	 * @brief Helper to create a vector of BindGroupLayoutEntries with auto binding assignment.
	 * @param name Optional name for the layout.
	 * @param rawEntries BindGroupLayoutEntry objects (with binding=-1 for auto).
	 * @return WebGPUBindGroupLayoutInfo containing the layout and descriptor.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> createBindGroupLayoutInfo(std::string name, std::vector<wgpu::BindGroupLayoutEntry> entries)
	{
		uint32_t nextBinding = 0;

		for (auto &entry : entries)
		{
			if (entry.binding == static_cast<uint32_t>(-1))
			{
				entry.binding = nextBinding++;
			}
			else
			{
				nextBinding = std::max(nextBinding, entry.binding + 1);
			}
		}
		wgpu::BindGroupLayoutDescriptor desc = createBindGroupLayoutDescriptor(entries);
		desc.label = name.c_str();
		auto layout = createBindGroupLayoutFromDescriptor(desc);
		return std::make_shared<WebGPUBindGroupLayoutInfo>(layout, desc);
	}

	/**
	 * @brief Helper to create a vector of BindGroupLayoutEntries with auto binding assignment.
	 * @tparam Entries Variadic BindGroupLayoutEntry arguments.
	 * @param rawEntries BindGroupLayoutEntry objects (with binding=-1 for auto).
	 * @return WebGPUBindGroupLayoutInfo containing the layout and descriptor.
	 */
	template <typename... Entries>
	std::shared_ptr<WebGPUBindGroupLayoutInfo> createBindGroupLayoutInfo(std::string name, Entries &&...rawEntries)
	{
		static_assert((std::is_convertible_v<Entries, wgpu::BindGroupLayoutEntry> && ...), "All arguments must be convertible to wgpu::BindGroupLayoutEntry");

		std::vector<wgpu::BindGroupLayoutEntry> entries = {std::forward<Entries>(rawEntries)...};
		return createBindGroupLayoutInfo(name, entries);
	}

	/**
	 * @brief Generic bind group creation from entries.
	 */
	wgpu::BindGroup createBindGroup(
		const wgpu::BindGroupLayout &layout,
		const std::vector<wgpu::BindGroupEntry> &entries
	);

	/**
	 * @brief Create a WebGPUBindGroup from layout info and optional material.
	 * @param layoutInfo The bind group layout info.
	 * @param material Optional material for per-material resources.
	 * @return Shared pointer to the created WebGPUBindGroup, or nullptr if not ready.
	 */
	std::shared_ptr<WebGPUBindGroup> createBindGroup(
		const std::shared_ptr<WebGPUBindGroupLayoutInfo> &layoutInfo,
		const std::shared_ptr<WebGPUMaterial> &material = nullptr
	);

	/**
	 * @brief Release all created bind groups and layouts.
	 */
	void cleanup()
	{
		m_globalBindGroups.clear();
		m_globalBindGroupLayouts.clear();
	}

	/**
	 * @brief Get global bind group by key.
	 * @param key Unique key for the bind group.
	 * @return Shared pointer to the WebGPUBindGroup, or nullptr if not found.
	 */
	std::shared_ptr<WebGPUBindGroup> getGlobalBindGroup(const std::string &key)
	{
		auto it = m_globalBindGroups.find(key);
		if (it == m_globalBindGroups.end())
		{
			return nullptr;
		}
		return it->second;
	}

	/**
	 * @brief Get global bind group layout by key.
	 * @param key Unique key for the bind group layout.
	 * @return Shared pointer to the WebGPUBindGroupLayoutInfo, or nullptr if not found.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> getGlobalBindGroupLayout(
		const std::string &key
	)
	{
		auto it = m_globalBindGroupLayouts.find(key);
		if (it == m_globalBindGroupLayouts.end())
		{
			return nullptr;
		}
		return it->second;
	}

	/**
	 * @brief Store a global bind group with a unique key.
	 * @param key Unique key for the bind group.
	 * @param bindGroup Shared pointer to the WebGPUBindGroup.
	 * @return True if stored successfully, false if key already exists.
	 */
	bool storeGlobalBindGroup(
		const std::string &key,
		const std::shared_ptr<WebGPUBindGroup> &bindGroup
	)
	{
		if (m_globalBindGroups.find(key) != m_globalBindGroups.end())
		{
			return false; // Key already exists
		}
		m_globalBindGroups[key] = bindGroup;
		return true;
	}

	/**
	 * @brief Store a global bind group layout with a unique key.
	 * @param key Unique key for the bind group layout.
	 * @param layoutInfo Shared pointer to the WebGPUBindGroupLayoutInfo.
	 * @return True if stored successfully, false if key already exists.
	 */
	bool storeGlobalBindGroupLayout(
		const std::string &key,
		const std::shared_ptr<WebGPUBindGroupLayoutInfo> &layoutInfo
	)
	{
		if (m_globalBindGroupLayouts.find(key) != m_globalBindGroupLayouts.end())
		{
			return false; // Key already exists
		}
		m_globalBindGroupLayouts[key] = layoutInfo;
		return true;
	}

  protected:
	/**
	 * @brief Create a BindGroupLayoutDescriptor from entries.
	 */
	wgpu::BindGroupLayoutDescriptor createBindGroupLayoutDescriptor(const std::vector<wgpu::BindGroupLayoutEntry> &entries);

	/**
	 * @brief Create a BindGroupDescriptor from layout and entries.
	 */
	wgpu::BindGroupDescriptor createBindGroupDescriptor(const wgpu::BindGroupLayout &layout, const std::vector<wgpu::BindGroupEntry> &entries);

	/**
	 * @brief Create a BindGroupLayout from a descriptor.
	 */
	wgpu::BindGroupLayout createBindGroupLayoutFromDescriptor(const wgpu::BindGroupLayoutDescriptor &desc);

	/**
	 * @brief Create a BindGroup from a descriptor.
	 */
	wgpu::BindGroup createBindGroupFromDescriptor(const wgpu::BindGroupDescriptor &desc);

  private:
	WebGPUContext &m_context;

	std::unordered_map<std::string, std::shared_ptr<WebGPUBindGroup>> m_globalBindGroups;
	std::unordered_map<std::string, std::shared_ptr<WebGPUBindGroupLayoutInfo>> m_globalBindGroupLayouts;
};

} // namespace engine::rendering::webgpu
