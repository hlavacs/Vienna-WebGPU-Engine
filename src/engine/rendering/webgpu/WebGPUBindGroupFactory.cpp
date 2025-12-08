#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroup.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"

#include <cassert>
#include <spdlog/spdlog.h>

#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUBindGroupFactory::WebGPUBindGroupFactory(WebGPUContext &context) :
	m_context(context) {}

WebGPUBindGroupFactory::~WebGPUBindGroupFactory()
{
	cleanup();
}

wgpu::BindGroupLayoutEntry WebGPUBindGroupFactory::createStorageBindGroupLayoutEntry(
	int binding,
	uint32_t visibility,
	bool readOnly
)
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
	wgpu::SamplerBindingType samplerType
)
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
	bool multisampled
)
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

std::shared_ptr<WebGPUBindGroupLayoutInfo> WebGPUBindGroupFactory::createDefaultMaterialBindGroupLayout()
{
	return createCustomBindGroupLayout(
		createUniformBindGroupLayoutEntry<Material::MaterialProperties>(-1, wgpu::ShaderStage::Fragment),
		createSamplerBindGroupLayoutEntry(-1, wgpu::ShaderStage::Fragment),
		createTextureBindGroupLayoutEntry(-1, wgpu::ShaderStage::Fragment), // Diffuse texture
		createTextureBindGroupLayoutEntry(-1, wgpu::ShaderStage::Fragment)	 // Normal texture
	);
}

std::shared_ptr<WebGPUBindGroupLayoutInfo> WebGPUBindGroupFactory::createDefaultLightingBindGroupLayout()
{
	return createCustomBindGroupLayout(
		createStorageBindGroupLayoutEntry(0, wgpu::ShaderStage::Fragment, true)
	);
}

// === Generic Bind Group Creation ===
wgpu::BindGroup WebGPUBindGroupFactory::createBindGroup(
	const wgpu::BindGroupLayout &layout,
	const std::vector<wgpu::BindGroupEntry> &entries
)
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
	const wgpu::Sampler &sampler
)
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
		 {normalEntry}}
	);

	return group;
}

wgpu::BindGroup WebGPUBindGroupFactory::createLightingBindGroup(
	const wgpu::BindGroupLayout &layout,
	const wgpu::Buffer &lightingBuffer
)
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

wgpu::BindGroup WebGPUBindGroupFactory::createBindGroupFromLayout(
	const WebGPUBindGroupLayoutInfo &layoutInfo,
	const std::vector<wgpu::Buffer> &buffers
)
{
	std::vector<wgpu::BindGroupEntry> entries;
	entries.reserve(layoutInfo.getEntryCount());

	for (uint32_t i = 0; i < layoutInfo.getEntryCount(); ++i)
	{
		const auto layoutEntry = layoutInfo.getEntry(i);

		// Ensure we have enough buffers
		if (layoutEntry->binding >= buffers.size())
		{
			spdlog::error("Not enough buffers provided for binding {}", layoutEntry->binding);
			return nullptr;
		}

		wgpu::BindGroupEntry entry{};
		entry.binding = layoutEntry->binding;
		entry.buffer = buffers[layoutEntry->binding];
		entry.offset = 0;

		if (layoutEntry->buffer.minBindingSize > 0)
		{
			entry.size = layoutEntry->buffer.minBindingSize;
		}
		else
		{
			entry.size = 0;
		}

		entries.push_back(entry);
	}

	return createBindGroup(layoutInfo.getLayout(), entries);
}

wgpu::BindGroup WebGPUBindGroupFactory::createBindGroupFromLayout(
	const WebGPUBindGroupLayoutInfo &layoutInfo,
	const std::vector<std::pair<wgpu::Buffer, size_t>> &bufferSizes
)
{
	std::vector<wgpu::BindGroupEntry> entries;
	entries.reserve(layoutInfo.getEntryCount());

	for (uint32_t i = 0; i < layoutInfo.getEntryCount(); ++i)
	{
		const auto layoutEntry = layoutInfo.getEntry(i);

		// Ensure we have enough buffers
		if (layoutEntry->binding >= bufferSizes.size())
		{
			spdlog::error("Not enough buffers provided for binding {}", layoutEntry->binding);
			return nullptr;
		}

		const auto &[buffer, size] = bufferSizes[layoutEntry->binding];

		wgpu::BindGroupEntry entry{};
		entry.binding = layoutEntry->binding;
		entry.buffer = buffer;
		entry.offset = 0;
		entry.size = size;

		entries.push_back(entry);
	}

	return createBindGroup(layoutInfo.getLayout(), entries);
}

std::shared_ptr<WebGPUBindGroup> WebGPUBindGroupFactory::createBindGroupWithBuffers(
	std::shared_ptr<WebGPUBindGroupLayoutInfo> layoutInfo,
	const std::vector<size_t> &bufferSizes
)
{
	if (!layoutInfo)
	{
		spdlog::error("Invalid layout info provided");
		return nullptr;
	}

	std::vector<std::shared_ptr<WebGPUBuffer>> buffers;
	std::vector<wgpu::BindGroupEntry> entries;
	buffers.reserve(layoutInfo->getEntryCount());
	entries.reserve(layoutInfo->getEntryCount());

	for (uint32_t i = 0; i < layoutInfo->getEntryCount(); ++i)
	{
		const auto layoutEntry = layoutInfo->getEntry(i);

		// Determine buffer size
		size_t bufferSize = 0;
		if (i < bufferSizes.size() && bufferSizes[i] > 0)
		{
			bufferSize = bufferSizes[i];
		}
		else if (layoutEntry->buffer.minBindingSize > 0)
		{
			bufferSize = layoutEntry->buffer.minBindingSize;
		}
		else
		{
			spdlog::error("Cannot determine buffer size for binding {}", layoutEntry->binding);
			return nullptr;
		}

		// Create buffer using the buffer factory
		std::string bufferName = "Buffer_Binding" + std::to_string(layoutEntry->binding);
		auto buffer = m_context.bufferFactory().createBufferFromLayoutEntry(*layoutInfo, layoutEntry->binding, bufferName, false, bufferSize);

		if (!buffer || !buffer->isValid())
		{
			spdlog::error("Failed to create buffer for binding {}", layoutEntry->binding);
			return nullptr;
		}

		buffers.push_back(buffer);

		// Create bind group entry
		wgpu::BindGroupEntry entry{};
		entry.binding = layoutEntry->binding;
		entry.buffer = buffer->getBuffer();
		entry.offset = 0;
		entry.size = bufferSize;
		entries.push_back(entry);
	}

	// Create the bind group
	wgpu::BindGroup bindGroup = createBindGroup(layoutInfo->getLayout(), entries);

	if (!bindGroup)
	{
		spdlog::error("Failed to create bind group");
		return nullptr;
	}

	return std::make_shared<WebGPUBindGroup>(bindGroup, layoutInfo, buffers);
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
