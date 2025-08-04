#pragma once

#include <webgpu/webgpu.hpp>
#include <vector>
#include <memory>

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
			uint32_t visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment)
		{
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
			bool readOnly = true);
			
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
			wgpu::SamplerBindingType samplerType = wgpu::SamplerBindingType::Filtering);
			
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
			bool multisampled = false);

		/**
		 * @brief Helper to create a vector of BindGroupLayoutEntries with auto binding assignment.
		 * @tparam Entries Variadic BindGroupLayoutEntry arguments.
		 * @param rawEntries BindGroupLayoutEntry objects (with binding=-1 for auto).
		 * @return std::vector<wgpu::BindGroupLayoutEntry>
		 */
		template <typename... Entries>
		wgpu::BindGroupLayout createCustomBindGroupLayout(Entries &&...rawEntries)
		{
			static_assert((std::is_convertible_v<Entries, wgpu::BindGroupLayoutEntry> && ...),
						  "All arguments must be convertible to wgpu::BindGroupLayoutEntry");

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
			return layout;
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
		 */
		wgpu::BindGroupLayout createDefaultMaterialBindGroupLayout();

		/**
		 * @brief Create a default lighting BindGroupLayout (uniform buffer).
		 */
		wgpu::BindGroupLayout createDefaultLightingBindGroupLayout();

		// === Bind Groups ===

		/**
		 * @brief Create a bind group for materials (baseColor, normal, sampler).
		 */
		wgpu::BindGroup createMaterialBindGroup(
			const wgpu::BindGroupLayout &layout,
			const wgpu::Buffer &materialPropertiesBuffer,
			const wgpu::TextureView &baseColor,
			const wgpu::TextureView &normal,
			const wgpu::Sampler &sampler);

		/**
		 * @brief Create a bind group for lighting using a uniform buffer.
		 */
		wgpu::BindGroup createLightingBindGroup(
			const wgpu::BindGroupLayout &layout,
			const wgpu::Buffer &lightingBuffer);

		/**
		 * @brief Generic bind group creation from entries.
		 */
		wgpu::BindGroup createBindGroup(
			const wgpu::BindGroupLayout &layout,
			const std::vector<wgpu::BindGroupEntry> &entries);

        /**
		 * @brief Release all created bind groups and layouts.
		 */
		void cleanup();

	private:
		WebGPUContext &m_context;
		std::vector<wgpu::BindGroup> m_createdBindGroups;
		std::vector<wgpu::BindGroupLayout> m_createdBindGroupLayouts;
	};

} // namespace engine::rendering::webgpu
