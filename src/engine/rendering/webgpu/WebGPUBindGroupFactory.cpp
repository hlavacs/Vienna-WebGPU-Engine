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
		std::vector<wgpu::BindGroupLayoutEntry> entries;
		// Binding 0: Albedo Texture
		wgpu::BindGroupLayoutEntry albedoTexEntry = {};
		albedoTexEntry.binding = 0;
		albedoTexEntry.visibility = wgpu::ShaderStage::Fragment;
		albedoTexEntry.texture.sampleType = wgpu::TextureSampleType::Float;
		albedoTexEntry.texture.viewDimension = wgpu::TextureViewDimension::_2D;
		albedoTexEntry.texture.multisampled = false;
		entries.push_back(albedoTexEntry);

		// Binding 1: Normal Texture
		wgpu::BindGroupLayoutEntry normalTexEntry = {};
		normalTexEntry.binding = 1;
		normalTexEntry.visibility = wgpu::ShaderStage::Fragment;
		normalTexEntry.texture.sampleType = wgpu::TextureSampleType::Float;
		normalTexEntry.texture.viewDimension = wgpu::TextureViewDimension::_2D;
		normalTexEntry.texture.multisampled = false;
		entries.push_back(normalTexEntry);

		// Binding 2: Shared Sampler
		wgpu::BindGroupLayoutEntry samplerEntry = {};
		samplerEntry.binding = 2;
		samplerEntry.visibility = wgpu::ShaderStage::Fragment;
		samplerEntry.sampler.type = wgpu::SamplerBindingType::Filtering;
		entries.push_back(samplerEntry);

		wgpu::BindGroupLayoutDescriptor desc = createBindGroupLayoutDescriptor(entries);
		auto layout = createBindGroupLayoutFromDescriptor(desc);
		return layout;
	}

	wgpu::BindGroupLayout WebGPUBindGroupFactory::createDefaultLightingBindGroupLayout()
	{
		std::vector<wgpu::BindGroupLayoutEntry> entries;
		wgpu::BindGroupLayoutEntry entry = {};
		entry.binding = 0;
		entry.visibility = wgpu::ShaderStage::Fragment;
		entry.buffer.type = wgpu::BufferBindingType::Uniform;
		entry.buffer.minBindingSize = 0;
		entries.push_back(entry);

		wgpu::BindGroupLayoutDescriptor desc = createBindGroupLayoutDescriptor(entries);
		auto layout = createBindGroupLayoutFromDescriptor(desc);
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
		const wgpu::TextureView &baseColor,
		const wgpu::TextureView &normal,
		const wgpu::Sampler &sampler)
	{
		std::vector<wgpu::BindGroupEntry> entries;

		// Binding 0: baseColor
		wgpu::BindGroupEntry baseColorEntry = {};
		baseColorEntry.binding = 0;
		baseColorEntry.textureView = baseColor;
		entries.push_back(baseColorEntry);

		// Binding 1: normal
		wgpu::BindGroupEntry normalEntry = {};
		normalEntry.binding = 1;
		normalEntry.textureView = normal;
		entries.push_back(normalEntry);

		// Binding 2: sampler
		wgpu::BindGroupEntry samplerEntry = {};
		samplerEntry.binding = 2;
		samplerEntry.sampler = sampler;
		entries.push_back(samplerEntry);

		wgpu::BindGroup group = createBindGroup(layout, entries); 
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
