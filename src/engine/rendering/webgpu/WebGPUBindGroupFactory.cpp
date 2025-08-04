#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"

#include <cassert>

#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

	WebGPUBindGroupFactory::WebGPUBindGroupFactory(WebGPUContext &context)
		: m_context(context) {}

	WebGPUBindGroupFactory::~WebGPUBindGroupFactory()
	{
		cleanup();
	}

	wgpu::BindGroupLayoutEntry WebGPUBindGroupFactory::createStorageBindGroupLayoutEntry(
		int binding,
		uint32_t visibility,
		bool readOnly)
	{
		wgpu::BindGroupLayoutEntry entry = {};
		entry.binding = static_cast<uint32_t>(binding); // will be replaced later if -1
		entry.visibility = visibility;
		entry.buffer.type = readOnly ? wgpu::BufferBindingType::ReadOnlyStorage
									 : wgpu::BufferBindingType::Storage;
		entry.buffer.hasDynamicOffset = false;
		entry.buffer.minBindingSize = 0; // No minimum size for storage buffer
		return entry;
	}

	wgpu::BindGroupLayoutEntry WebGPUBindGroupFactory::createSamplerBindGroupLayoutEntry(
		int binding,
		uint32_t visibility,
		wgpu::SamplerBindingType samplerType)
	{
		wgpu::BindGroupLayoutEntry entry = {};
		entry.binding = static_cast<uint32_t>(binding); // will be replaced later if -1
		entry.visibility = visibility;
		entry.sampler.type = samplerType;
		return entry;
	}

	wgpu::BindGroupLayoutEntry WebGPUBindGroupFactory::createTextureBindGroupLayoutEntry(
		int binding,
		uint32_t visibility,
		wgpu::TextureSampleType sampleType,
		wgpu::TextureViewDimension viewDimension,
		bool multisampled)
	{
		wgpu::BindGroupLayoutEntry entry = {};
		entry.binding = static_cast<uint32_t>(binding); // will be replaced later if -1
		entry.visibility = visibility;
		entry.texture.sampleType = sampleType;
		entry.texture.viewDimension = viewDimension;
		entry.texture.multisampled = multisampled;
		return entry;
	}

	// === Descriptor creation ===

	wgpu::BindGroupLayoutDescriptor WebGPUBindGroupFactory::createBindGroupLayoutDescriptor(const std::vector<wgpu::BindGroupLayoutEntry> &entries)
	{
		wgpu::BindGroupLayoutDescriptor desc = {};
		desc.entryCount = static_cast<uint32_t>(entries.size());
		desc.entries = entries.data();
		return desc;
	}

	wgpu::BindGroupDescriptor WebGPUBindGroupFactory::createBindGroupDescriptor(const wgpu::BindGroupLayout &layout, const std::vector<wgpu::BindGroupEntry> &entries)
	{
		wgpu::BindGroupDescriptor desc = {};
		desc.layout = layout;
		desc.entryCount = static_cast<uint32_t>(entries.size());
		desc.entries = entries.data();
		return desc;
	}

	// === Creation from descriptors ===

	wgpu::BindGroupLayout WebGPUBindGroupFactory::createBindGroupLayoutFromDescriptor(const wgpu::BindGroupLayoutDescriptor &desc)
	{
		auto layout = m_context.getDevice().createBindGroupLayout(desc);
		m_createdBindGroupLayouts.push_back(layout);
		return layout;
	}

	wgpu::BindGroup WebGPUBindGroupFactory::createBindGroupFromDescriptor(const wgpu::BindGroupDescriptor &desc)
	{
		auto group = m_context.getDevice().createBindGroup(desc);
		m_createdBindGroups.push_back(group);
		return group;
	}
	// === Custom/Default Layouts ===

	wgpu::BindGroupLayout WebGPUBindGroupFactory::createDefaultMaterialBindGroupLayout()
	{

		wgpu::BindGroupLayout layout = createCustomBindGroupLayout(
			createUniformBindGroupLayoutEntry<Material::MaterialProperties>(),
			createSamplerBindGroupLayoutEntry(),
			createTextureBindGroupLayoutEntry(),  // Diffuse texture
			createTextureBindGroupLayoutEntry()); // Normal texture
		return layout;
	}

	wgpu::BindGroupLayout WebGPUBindGroupFactory::createDefaultLightingBindGroupLayout()
	{
		auto layout = createCustomBindGroupLayout(
			createStorageBindGroupLayoutEntry(0, wgpu::ShaderStage::Fragment, true));
		return layout;
	}

	// === Generic Bind Group Creation ===
	wgpu::BindGroup WebGPUBindGroupFactory::createBindGroup(
		const wgpu::BindGroupLayout &layout,
		const std::vector<wgpu::BindGroupEntry> &entries)
	{
		wgpu::BindGroupDescriptor desc = createBindGroupDescriptor(layout, entries);
		auto group = createBindGroupFromDescriptor(desc);
		return group;
	}

	wgpu::BindGroup WebGPUBindGroupFactory::createMaterialBindGroup(
		const wgpu::BindGroupLayout &layout,
		const wgpu::Buffer &materialPropertiesBuffer,
		const wgpu::TextureView &baseColor,
		const wgpu::TextureView &normal,
		const wgpu::Sampler &sampler)
	{
		wgpu::BindGroupEntry materialPropertiesEntry = {};
		materialPropertiesEntry.binding = 0;
		materialPropertiesEntry.buffer = materialPropertiesBuffer;
		materialPropertiesEntry.offset = 0;
		materialPropertiesEntry.size = sizeof(Material::MaterialProperties);

		wgpu::BindGroupEntry samplerEntry = {};
		samplerEntry.binding = 1;
		samplerEntry.sampler = sampler;

		wgpu::BindGroupEntry baseColorEntry = {};
		baseColorEntry.binding = 2;
		baseColorEntry.textureView = baseColor;

		wgpu::BindGroupEntry normalEntry = {};
		normalEntry.binding = 3;
		normalEntry.textureView = normal;

		wgpu::BindGroup group = createBindGroup(
			layout,
			{{materialPropertiesEntry},
			 {samplerEntry},
			 {baseColorEntry},
			 {normalEntry}});

		return group;
	}

	wgpu::BindGroup WebGPUBindGroupFactory::createLightingBindGroup(
		const wgpu::BindGroupLayout &layout,
		const wgpu::Buffer &lightingBuffer)
	{
		std::vector<wgpu::BindGroupEntry> entries;
		wgpu::BindGroupEntry entry = {};
		entry.binding = 0;
		entry.buffer = lightingBuffer;
		entry.offset = 0;
		entry.size = 0; // Set to 0, or set to actual size if needed
		entries.push_back(entry);

		wgpu::BindGroup group = createBindGroup(layout, entries);
		return group;
	}

	void WebGPUBindGroupFactory::cleanup()
	{
		for (auto &group : m_createdBindGroups)
		{
			if (group)
				group.release();
		}
		m_createdBindGroups.clear();
		for (auto &layout : m_createdBindGroupLayouts)
		{
			if (layout)
				layout.release();
		}
		m_createdBindGroupLayouts.clear();
	}

} // namespace engine::rendering::webgpu
