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

std::shared_ptr<WebGPUBindGroup> WebGPUBindGroupFactory::createBindGroup(
	const std::shared_ptr<WebGPUBindGroupLayoutInfo> &layoutInfo,
	const std::shared_ptr<WebGPUMaterial> &material
)
{

	auto globalKey = layoutInfo->getKey();
	if (globalKey.has_value())
	{
		auto globalBindGroup = getGlobalBindGroup(globalKey.value_or(""));
		if (globalBindGroup)
		{
			spdlog::debug("Reusing cached global bind group '{}'", globalKey.value());
			return globalBindGroup;
		}
	}
	std::vector<std::shared_ptr<WebGPUBuffer>> groupBuffers;
	std::vector<wgpu::BindGroupEntry> entries;
	bool hasTextures = false;
	bool hasSamplers = false;
	bool hasBuffers = false;

	bool allReady = true;
	for (auto entryLayout : layoutInfo->getEntries())
	{
		wgpu::BindGroupEntry entry{};
		entry.binding = entryLayout.binding;

		if (entryLayout.buffer.type != wgpu::BufferBindingType::Undefined)
		{
			auto buffer = m_context.bufferFactory().createBufferFromLayoutEntry(
				*layoutInfo,
				entryLayout.binding,
				"BindGroupBuffer_" + std::to_string(entryLayout.binding),
				entryLayout.buffer.minBindingSize

			);
			groupBuffers.push_back(buffer);
			if (!buffer)
			{
				allReady = false;
				continue;
			}
			entry.buffer = buffer->getBuffer();
			entry.offset = 0;
			entry.size = entryLayout.buffer.minBindingSize;
		}
		else if (entryLayout.texture.sampleType != wgpu::TextureSampleType::Undefined)
		{
			if (!material)
			{
				allReady = false;
				spdlog::debug("Bind group requires material for texture bindings");
				break;
			}
			std::string slotName = layoutInfo->getMaterialSlotName(entryLayout.binding);
			auto tex = material->getTexture(slotName);
			if (tex)
			{
				entry.textureView = tex->getTextureView();
			}
			else
			{
				auto fallbackColor = layoutInfo->getFallbackColor(entryLayout.binding);
				if (fallbackColor.has_value())
				{
					entry.textureView = m_context.textureFactory().createFromColor(fallbackColor.value())->getTextureView();
				}
				else
				{
					allReady = false;
					spdlog::debug("Texture for slot '{}' not ready", slotName);
					continue;
				}
			}
		}
		else if (entryLayout.sampler.type != wgpu::SamplerBindingType::Undefined)
		{
			entry.sampler = m_context.getDefaultSampler();
		}

		entries.push_back(entry);
	}

	if (!allReady)
	{
		spdlog::debug("Bind group not ready - missing resources");
		return nullptr;
	}

	// Create the raw WebGPU bind group
	wgpu::BindGroup rawBindGroup = createBindGroup(
		layoutInfo->getLayout(),
		entries
	);

	auto bindGroup = std::make_shared<WebGPUBindGroup>(
		rawBindGroup,
		layoutInfo,
		groupBuffers
	);
	if (globalKey.has_value())
		storeGlobalBindGroup(globalKey.value(), bindGroup);

	return bindGroup;
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
	return layout;
}

wgpu::BindGroup WebGPUBindGroupFactory::createBindGroupFromDescriptor(const wgpu::BindGroupDescriptor &desc)
{
	auto group = m_context.getDevice().createBindGroup(desc);
	return group;
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

} // namespace engine::rendering::webgpu
