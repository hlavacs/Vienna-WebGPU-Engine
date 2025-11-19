#pragma once

#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"

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

	// === Utility: Bind Group Layout Entry Creation ===

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
	 * @tparam Entries Variadic BindGroupLayoutEntry arguments.
	 * @param rawEntries BindGroupLayoutEntry objects (with binding=-1 for auto).
	 * @return WebGPUBindGroupLayoutInfo containing the layout and descriptor.
	 */
	template <typename... Entries>
	std::shared_ptr<WebGPUBindGroupLayoutInfo> createCustomBindGroupLayout(Entries &&...rawEntries)
	{
		static_assert((std::is_convertible_v<Entries, wgpu::BindGroupLayoutEntry> && ...), "All arguments must be convertible to wgpu::BindGroupLayoutEntry");

		std::vector<wgpu::BindGroupLayoutEntry> entries = {std::forward<Entries>(rawEntries)...};
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
		auto layout = createBindGroupLayoutFromDescriptor(desc);
		return std::make_shared<WebGPUBindGroupLayoutInfo>(layout, desc);
	}

	// === Descriptor creation ===

	/**
	 * @brief Create a BindGroupLayoutDescriptor from entries.
	 */
	wgpu::BindGroupLayoutDescriptor createBindGroupLayoutDescriptor(const std::vector<wgpu::BindGroupLayoutEntry> &entries);

	/**
	 * @brief Create a BindGroupDescriptor from layout and entries.
	 */
	wgpu::BindGroupDescriptor createBindGroupDescriptor(const wgpu::BindGroupLayout &layout, const std::vector<wgpu::BindGroupEntry> &entries);

	// === Creation from descriptors ===

	/**
	 * @brief Create a BindGroupLayout from a descriptor.
	 */
	wgpu::BindGroupLayout createBindGroupLayoutFromDescriptor(const wgpu::BindGroupLayoutDescriptor &desc);

	/**
	 * @brief Create a BindGroup from a descriptor.
	 */
	wgpu::BindGroup createBindGroupFromDescriptor(const wgpu::BindGroupDescriptor &desc);

	// === Bind Group Layouts ===

	/**
	 * @brief Create a default material BindGroupLayout (albedo, normal, sampler).
	 * @return WebGPUBindGroupLayoutInfo containing the layout and descriptor.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> createDefaultMaterialBindGroupLayout();

	/**
	 * @brief Create a default lighting BindGroupLayout (uniform buffer).
	 * @return WebGPUBindGroupLayoutInfo containing the layout and descriptor.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> createDefaultLightingBindGroupLayout();

	// === Bind Groups ===

	/**
	 * @brief Create a bind group for materials (baseColor, normal, sampler).
	 */
	wgpu::BindGroup createMaterialBindGroup(
		const wgpu::BindGroupLayout &layout,
		const wgpu::Buffer &materialPropertiesBuffer,
		const wgpu::TextureView &baseColor,
		const wgpu::TextureView &normal,
		const wgpu::Sampler &sampler
	);

	/**
	 * @brief Create a bind group for lighting using a uniform buffer.
	 */
	wgpu::BindGroup createLightingBindGroup(
		const wgpu::BindGroupLayout &layout,
		const wgpu::Buffer &lightingBuffer
	);

	/**
	 * @brief Generic bind group creation from entries.
	 */
	wgpu::BindGroup createBindGroup(
		const wgpu::BindGroupLayout &layout,
		const std::vector<wgpu::BindGroupEntry> &entries
	);

	/**
	 * @brief Creates a bind group from layout info and buffers.
	 * Automatically constructs bind group entries based on the layout.
	 * @param layoutInfo The bind group layout info containing entry metadata.
	 * @param buffers Buffers to bind, in order of binding indices.
	 * @return The created bind group.
	 */
	wgpu::BindGroup createBindGroupFromLayout(
		const WebGPUBindGroupLayoutInfo &layoutInfo,
		const std::vector<wgpu::Buffer> &buffers
	);

	/**
	 * @brief Creates a bind group from layout info with explicit buffer sizes.
	 * Useful when you need to specify exact buffer ranges.
	 * @param layoutInfo The bind group layout info containing entry metadata.
	 * @param bufferSizes Pairs of buffer and size for each binding.
	 * @return The created bind group.
	 */
	wgpu::BindGroup createBindGroupFromLayout(
		const WebGPUBindGroupLayoutInfo &layoutInfo,
		const std::vector<std::pair<wgpu::Buffer, size_t>> &bufferSizes
	);

	/**
	 * @brief Release all created bind groups and layouts.
	 */
	void cleanup();

	/**
	 * @brief Creates a complete bind group with buffers from layout info.
	 * Automatically creates all buffers and the bind group based on the layout.
	 * @param layoutInfo The bind group layout info containing entry metadata.
	 * @param bufferSizes Optional sizes for each buffer. If empty, uses minBindingSize from layout.
	 * @return Shared pointer to the created bind group.
	 */
	std::shared_ptr<WebGPUBindGroup> createBindGroupWithBuffers(
		std::shared_ptr<WebGPUBindGroupLayoutInfo> layoutInfo,
		const std::vector<size_t> &bufferSizes = {}
	);

  private:
	WebGPUContext &m_context;
	std::vector<wgpu::BindGroup> m_createdBindGroups;
	std::vector<wgpu::BindGroupLayout> m_createdBindGroupLayouts;
};

} // namespace engine::rendering::webgpu
