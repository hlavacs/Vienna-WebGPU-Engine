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

std::shared_ptr<WebGPUBindGroupLayoutInfo> WebGPUBindGroupFactory::createBindGroupLayoutInfo(
	std::string name,
	BindGroupType type,
	BindGroupReuse reuse,
	std::vector<wgpu::BindGroupLayoutEntry> entries,
	std::vector<BindGroupBinding> bindings
)
{
	uint32_t nextBinding = 0;
	bindings.reserve(entries.size());
	for (auto &entry : entries)
	{
		if (entry.binding == static_cast<uint32_t>(-1))
			entry.binding = nextBinding++;
		else
			nextBinding = std::max(nextBinding, entry.binding + 1);
	}
	wgpu::BindGroupLayoutDescriptor desc = createBindGroupLayoutDescriptor(entries);
	desc.label = name.c_str();
	auto layout = m_context.getDevice().createBindGroupLayout(desc);
	return std::make_shared<WebGPUBindGroupLayoutInfo>(
		layout,
		desc,
		std::move(name),
		type,
		reuse,
		std::move(bindings)
	);
}

std::shared_ptr<WebGPUBindGroup> WebGPUBindGroupFactory::createBindGroup(
	const std::shared_ptr<WebGPUBindGroupLayoutInfo> &layoutInfo,
	const std::map<BindGroupBindingKey, BindGroupResource> &resources,
	const std::shared_ptr<WebGPUMaterial> &material,
	const char *label
)
{
	std::vector<std::shared_ptr<WebGPUBuffer>> groupBuffers;
	std::vector<wgpu::BindGroupEntry> entries;
	entries.reserve(layoutInfo->getBindings().size());

	bool allReady = true;

	for (auto entryLayout : layoutInfo->getEntries())
	{
		wgpu::BindGroupEntry entry{};
		entry.binding = entryLayout.binding;

		// Check if this binding has an override in the resources map
		// Note: We don't know the group index from layoutInfo, so we check all keys with matching binding
		auto resourceIt = std::find_if(resources.begin(), resources.end(), [&entryLayout](const auto &pair)
									   { return std::get<1>(pair.first) == entryLayout.binding; });
		bool hasOverride = (resourceIt != resources.end());

		if (hasOverride)
		{
			// Use the provided override resource
			const auto &bindResource = resourceIt->second;
			std::visit([&entry, &groupBuffers](const auto &resource)
					   {
				using T = std::decay_t<decltype(resource)>;
				
				if constexpr (std::is_same_v<T, std::shared_ptr<WebGPUTexture>>)
				{
					entry.textureView = resource->getTextureView();
				}
				else if constexpr (std::is_same_v<T, std::shared_ptr<WebGPUSampler>>)
				{
					// Pass the raw wgpu handle into the descriptor; wgpu's
					// createBindGroup internally references the sampler so
					// it stays alive for the bind group's lifetime even if
					// the shared_ptr drops afterwards. The factory's
					// SlotCache slot (or the consumer's shared_ptr) keeps
					// the WebGPUSampler RAII wrapper alive across this call.
					entry.sampler = resource ? resource->raw() : wgpu::Sampler(nullptr);
				}
				else if constexpr (std::is_same_v<T, std::shared_ptr<WebGPUBuffer>>)
				{
					entry.buffer = resource->getBuffer();
					entry.offset = 0;
					entry.size = resource->getSize();
					groupBuffers.push_back(resource);
				} },
					   bindResource.resource);
		}
		else
		{
			// No override - create resource automatically based on layout
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
					spdlog::warn("Bind group requires material for texture bindings");
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
					auto fallbackColor = layoutInfo->getMaterialFallbackColor(entryLayout.binding);
					if (fallbackColor.has_value())
					{
						entry.textureView = m_context.textureFactory().createFromColor(fallbackColor.value())->getTextureView();
					}
					else
					{
						allReady = false;
						spdlog::warn("Texture for slot '{}' not ready", slotName);
						continue;
					}
				}
			}
			else if (entryLayout.sampler.type != wgpu::SamplerBindingType::Undefined)
			{
				// Factory's slot keeps the WebGPUSampler alive across this
				// expression; createBindGroup bumps the wgpu refcount
				// internally so the bind group survives Clear All.
				entry.sampler = m_context.samplerFactory().getDefaultSampler()->raw();
			}
		}

		entries.push_back(entry);
	}

	if (!allReady)
	{
		spdlog::warn("Bind group not ready - missing resources");
		return nullptr;
	}

	std::string labelStr = label ? std::string(label) : "BindGroup";
	wgpu::BindGroupDescriptor desc{};
	desc.layout = layoutInfo->getLayout();
	desc.entryCount = static_cast<uint32_t>(entries.size());
	desc.entries = entries.data();
	desc.label = labelStr.c_str();

	wgpu::BindGroup rawBindGroup = m_context.getDevice().createBindGroup(desc);

	auto bindGroup = std::make_shared<WebGPUBindGroup>(
		rawBindGroup,
		layoutInfo,
		groupBuffers
	);

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

/**
 * @brief Get global bind group by key.
 * @param key Unique key for the bind group.
 * @return Shared pointer to the WebGPUBindGroup, or nullptr if not found.
 */
std::shared_ptr<WebGPUBindGroup> WebGPUBindGroupFactory::getGlobalBindGroup(const std::string &key)
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
std::shared_ptr<WebGPUBindGroupLayoutInfo> WebGPUBindGroupFactory::getGlobalBindGroupLayout(
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
bool WebGPUBindGroupFactory::storeGlobalBindGroup(
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
bool WebGPUBindGroupFactory::storeGlobalBindGroupLayout(
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

wgpu::BindGroupLayout WebGPUBindGroupFactory::createBindGroupLayout(
	const std::vector<wgpu::BindGroupLayoutEntry> &entries,
	const char *label
)
{
	wgpu::BindGroupLayoutDescriptor desc = createBindGroupLayoutDescriptor(entries);
	desc.label = label;
	return m_context.getDevice().createBindGroupLayout(desc);
}

// === Generic Bind Group Creation ===

wgpu::BindGroup WebGPUBindGroupFactory::createBindGroup(
	const wgpu::BindGroupLayout &layout,
	const std::vector<wgpu::BindGroupEntry> &entries
)
{
	wgpu::BindGroupDescriptor desc = createBindGroupDescriptor(layout, entries);
	auto group = m_context.getDevice().createBindGroup(desc);
	return group;
}

std::shared_ptr<WebGPUBindGroup> WebGPUBindGroupFactory::createBindGroup(
	const std::shared_ptr<WebGPUBindGroupLayoutInfo> &layoutInfo,
	const std::vector<wgpu::BindGroupEntry> &entries,
	std::vector<std::shared_ptr<WebGPUBuffer>> buffers
)
{
	wgpu::BindGroupDescriptor desc = createBindGroupDescriptor(layoutInfo->getLayout(), entries);
	wgpu::BindGroup group = m_context.getDevice().createBindGroup(desc);
	return std::make_shared<WebGPUBindGroup>(group, layoutInfo, std::move(buffers));
}

} // namespace engine::rendering::webgpu
