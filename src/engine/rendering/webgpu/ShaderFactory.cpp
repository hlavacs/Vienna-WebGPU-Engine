#include "engine/rendering/webgpu/ShaderFactory.h"

#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/resources/ResourceManager.h"

namespace engine::rendering::webgpu
{

ShaderFactory::ShaderFactory(WebGPUContext &context) : m_context(context), m_shaderInfo(nullptr)
{
}

ShaderFactory &ShaderFactory::begin(
	const std::string &name,
	const std::string &vertexEntry,
	const std::string &fragmentEntry,
	const std::filesystem::path &shaderPath
)
{
	// Reset state for new shader
	m_shaderInfo = std::make_shared<WebGPUShaderInfo>();
	m_shaderInfo->setName(name);
	m_shaderInfo->setVertexEntryPoint(vertexEntry);
	m_shaderInfo->setFragmentEntryPoint(fragmentEntry);
	m_shaderPath = shaderPath;
	m_bindGroupsBuilder.clear();

	return *this;
}

ShaderFactory &ShaderFactory::setShaderModule(wgpu::ShaderModule module)
{
	m_shaderInfo->setModule(module);
	return *this;
}

ShaderFactory &ShaderFactory::addFrameUniforms(uint32_t groupIndex, uint32_t binding)
{
	auto &bindings = getOrCreateBindGroup(groupIndex);

	ShaderBinding buffer;
	buffer.type = BindingType::UniformBuffer;
	buffer.name = "frameUniforms";
	buffer.binding = binding;
	buffer.size = sizeof(engine::rendering::FrameUniforms);
	buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	buffer.isGlobal = true;
	buffer.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;

	bindings.push_back(buffer);
	return *this;
}

ShaderFactory &ShaderFactory::addLightUniforms(uint32_t groupIndex, uint32_t binding, size_t maxLights)
{
	auto &bindings = getOrCreateBindGroup(groupIndex);

	// Calculate buffer size: header + light array
	size_t headerSize = sizeof(engine::rendering::LightsBuffer);
	size_t lightArraySize = maxLights * sizeof(engine::rendering::LightStruct);
	size_t totalSize = headerSize + lightArraySize;

	ShaderBinding buffer;
	buffer.type = BindingType::StorageBuffer;
	buffer.name = "lightUniforms";
	buffer.binding = binding;
	buffer.size = totalSize;
	buffer.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	buffer.isGlobal = true;
	buffer.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	buffer.readOnly = true;

	bindings.push_back(buffer);
	return *this;
}

ShaderFactory &ShaderFactory::addCameraUniforms(uint32_t groupIndex, uint32_t binding)
{
	auto &bindings = getOrCreateBindGroup(groupIndex);

	// Camera uniforms are typically part of frame uniforms, but can be separate
	ShaderBinding buffer;
	buffer.type = BindingType::UniformBuffer;
	buffer.name = "cameraUniforms";
	buffer.binding = binding;
	buffer.size = sizeof(glm::mat4) * 2 + sizeof(glm::vec4); // view + projection + position
	buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	buffer.isGlobal = true;
	buffer.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;

	bindings.push_back(buffer);
	return *this;
}

ShaderFactory &ShaderFactory::addCustomUniform(
	const std::string &name,
	size_t size,
	uint32_t groupIndex,
	uint32_t binding,
	bool isGlobal,
	uint32_t visibility
)
{
	auto &bindings = getOrCreateBindGroup(groupIndex);

	ShaderBinding buffer;
	buffer.type = BindingType::UniformBuffer;
	buffer.name = name;
	buffer.binding = binding;
	buffer.size = size;
	buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	buffer.isGlobal = isGlobal;
	buffer.visibility = visibility;

	bindings.push_back(buffer);
	return *this;
}

ShaderFactory &ShaderFactory::addStorageBuffer(
	const std::string &name,
	size_t size,
	uint32_t groupIndex,
	uint32_t binding,
	bool readOnly,
	bool isGlobal,
	uint32_t visibility
)
{
	auto &bindings = getOrCreateBindGroup(groupIndex);

	ShaderBinding buffer;
	buffer.type = BindingType::StorageBuffer;
	buffer.name = name;
	buffer.binding = binding;
	buffer.size = size;
	buffer.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	buffer.isGlobal = isGlobal;
	buffer.visibility = visibility;
	buffer.readOnly = readOnly;

	bindings.push_back(buffer);
	return *this;
}

ShaderFactory &ShaderFactory::addTexture(
	const std::string &name,
	const std::string &materialSlotName,
	uint32_t groupIndex,
	uint32_t binding,
	wgpu::TextureSampleType sampleType,
	wgpu::TextureViewDimension viewDimension,
	bool multisampled,
	uint32_t visibility
)
{
	auto &bindings = getOrCreateBindGroup(groupIndex);

	ShaderBinding textureBinding;
	textureBinding.type = BindingType::Texture;
	textureBinding.name = name;
	textureBinding.materialSlotName = materialSlotName;
	textureBinding.binding = binding;
	textureBinding.visibility = visibility;
	textureBinding.textureSampleType = sampleType;
	textureBinding.textureViewDimension = viewDimension;
	textureBinding.textureMultisampled = multisampled;

	bindings.push_back(textureBinding);
	return *this;
}

ShaderFactory &ShaderFactory::addSampler(
	const std::string &name,
	uint32_t groupIndex,
	uint32_t binding,
	wgpu::SamplerBindingType samplerType,
	uint32_t visibility
)
{
	auto &bindings = getOrCreateBindGroup(groupIndex);

	ShaderBinding samplerBinding;
	samplerBinding.type = BindingType::Sampler;
	samplerBinding.name = name;
	samplerBinding.binding = binding;
	samplerBinding.visibility = visibility;
	samplerBinding.samplerType = samplerType;

	bindings.push_back(samplerBinding);
	return *this;
}

std::shared_ptr<WebGPUShaderInfo> ShaderFactory::build()
{
	// Load shader module if not already set and path provided
	if (!m_shaderInfo->getModule() && !m_shaderPath.empty())
	{
		loadShaderModule();
	}

	// Validate shader module exists
	if (!m_shaderInfo->getModule())
	{
		spdlog::error("ShaderFactory::build() - No shader module set for shader '{}'", m_shaderInfo->getName());
		return nullptr;
	}

	// Create bind group layouts, buffers, and bind groups
	createBindGroupLayouts();
	createBuffersAndBindGroups();

	spdlog::info("ShaderFactory: Built shader '{}' with {} bind groups", m_shaderInfo->getName(), m_shaderInfo->getBindGroups().size());

	return m_shaderInfo;
}

std::vector<ShaderBinding> &ShaderFactory::getOrCreateBindGroup(uint32_t groupIndex)
{
	return m_bindGroupsBuilder[groupIndex];
}

void ShaderFactory::createBindGroupLayouts()
{
	for (auto &[groupIndex, bindings] : m_bindGroupsBuilder)
	{
		// Create layout entries from bindings
		std::vector<wgpu::BindGroupLayoutEntry> entries;
		entries.reserve(bindings.size());

		for (const auto &binding : bindings)
		{
			wgpu::BindGroupLayoutEntry entry{};

			// Cast visibility from uint32_t to wgpu::ShaderStage enum
			uint32_t vis = binding.visibility;
			uint32_t visibility = *reinterpret_cast<wgpu::ShaderStage *>(&vis);

			// Configure entry based on binding type using WebGPUBindGroupFactory helpers
			switch (binding.type)
			{
			case BindingType::UniformBuffer:
				entry.binding = binding.binding;
				entry.visibility = visibility;
				entry.buffer.type = wgpu::BufferBindingType::Uniform;
				entry.buffer.minBindingSize = binding.size;
				entry.buffer.hasDynamicOffset = false;
				break;

			case BindingType::StorageBuffer:
				entry = m_context.bindGroupFactory().createStorageBindGroupLayoutEntry(
					binding.binding,
					visibility,
					binding.readOnly
				);
				entry.buffer.minBindingSize = binding.size;
				break;

			case BindingType::Texture:
				entry = m_context.bindGroupFactory().createTextureBindGroupLayoutEntry(
					binding.binding,
					visibility,
					binding.textureSampleType,
					binding.textureViewDimension,
					binding.textureMultisampled
				);
				break;

			case BindingType::Sampler:
				entry = m_context.bindGroupFactory().createSamplerBindGroupLayoutEntry(
					binding.binding,
					visibility,
					binding.samplerType
				);
				break;
			}

			entries.push_back(entry);
		}

		// Create bind group layout using factory
		wgpu::BindGroupLayoutDescriptor desc = m_context.bindGroupFactory().createBindGroupLayoutDescriptor(entries);
		desc.label = (m_shaderInfo->getName() + "_bg_layout_" + std::to_string(groupIndex)).c_str();
		wgpu::BindGroupLayout layout = m_context.bindGroupFactory().createBindGroupLayoutFromDescriptor(desc);
		auto layoutInfo = std::make_shared<WebGPUBindGroupLayoutInfo>(layout, desc);

		// Store material slot names for texture bindings
		for (const auto &binding : bindings)
		{
			if (binding.type == BindingType::Texture && !binding.materialSlotName.empty())
			{
				layoutInfo->setMaterialSlotName(binding.binding, binding.materialSlotName);
			}
		}

		// Store in a temporary map for use in createGlobalBuffers
		m_tempLayouts[groupIndex] = layoutInfo;

		spdlog::debug("Created bind group layout {} for group {} with {} entries", groupIndex, groupIndex, entries.size());
	}
}

std::shared_ptr<WebGPUBuffer> ShaderFactory::createBuffer(const ShaderBinding &binding)
{
	// Textures and samplers don't need buffers
	if (binding.type == BindingType::Texture || binding.type == BindingType::Sampler)
	{
		return nullptr;
	}

	std::shared_ptr<WebGPUBuffer> buffer;

	// Check if this is a global buffer that's already cached
	if (binding.isGlobal)
	{
		auto it = m_globalBufferCache.find(binding.name);
		if (it != m_globalBufferCache.end())
		{
			spdlog::debug("Reusing cached global buffer '{}'", binding.name);
			return it->second;
		}
	}

	// Create new buffer based on type
	switch (binding.type)
	{
	case BindingType::UniformBuffer:
		buffer = m_context.bufferFactory().createUniformBufferWrapped(
			binding.name,
			binding.binding,
			binding.size,
			binding.isGlobal
		);
		break;

	case BindingType::StorageBuffer:
		buffer = m_context.bufferFactory().createStorageBufferWrapped(
			binding.name,
			binding.binding,
			binding.size,
			binding.isGlobal
		);
		break;

	default:
		spdlog::error("Unknown buffer type for binding '{}'", binding.name);
		return nullptr;
	}

	if (!buffer || !buffer->isValid())
	{
		spdlog::error("Failed to create {} buffer '{}'", binding.isGlobal ? "global" : "non-global", binding.name);
		return nullptr;
	}

	// Cache global buffers for reuse
	if (binding.isGlobal)
	{
		m_globalBufferCache[binding.name] = buffer;
	}

	spdlog::debug("Created {} buffer '{}' (size: {} bytes)", binding.isGlobal ? "global" : "non-global", binding.name, binding.size);

	return buffer;
}

void ShaderFactory::createBuffersAndBindGroups()
{
	for (auto &[groupIndex, bindings] : m_bindGroupsBuilder)
	{
		// Get the layout for this group
		auto layoutIt = m_tempLayouts.find(groupIndex);
		if (layoutIt == m_tempLayouts.end())
		{
			spdlog::error("No layout found for bind group {}", groupIndex);
			continue;
		}
		auto layoutInfo = layoutIt->second;

		std::vector<std::shared_ptr<WebGPUBuffer>> groupBuffers;
		std::vector<wgpu::BindGroupEntry> entries;
		bool hasTextures = false;
		bool hasSamplers = false;
		bool hasBuffers = false;

		// First pass: categorize bindings and create buffers
		for (const auto &binding : bindings)
		{
			switch (binding.type)
			{
			case BindingType::UniformBuffer:
			case BindingType::StorageBuffer:
			{
				hasBuffers = true;

				// Create buffer
				auto buffer = createBuffer(binding);
				if (!buffer)
				{
					spdlog::error("Failed to create buffer for binding {} in group {}", binding.binding, groupIndex);
					continue;
				}

				groupBuffers.push_back(buffer);

				// Create bind group entry for the buffer
				wgpu::BindGroupEntry entry{};
				entry.binding = binding.binding;
				entry.buffer = buffer->getBuffer();
				entry.offset = 0;
				entry.size = binding.size;
				entries.push_back(entry);
				break;
			}

			case BindingType::Texture:
				hasTextures = true;
				spdlog::debug("Bind group {} has texture '{}' at binding {} (lazy init by material)", groupIndex, binding.name, binding.binding);
				break;

			case BindingType::Sampler:
				hasSamplers = true;
				spdlog::debug("Bind group {} has sampler '{}' at binding {} (lazy init by material)", groupIndex, binding.name, binding.binding);
				break;
			}
		}

		// Determine if we can eagerly create the bind group
		bool hasNonBufferBindings = hasTextures || hasSamplers;
		wgpu::BindGroup rawBindGroup = nullptr;

		if (!hasNonBufferBindings && hasBuffers)
		{
			// Eager creation: This group only has buffers, create immediately
			wgpu::BindGroupDescriptor bgDesc{};
			bgDesc.layout = layoutInfo->getLayout();
			bgDesc.entryCount = entries.size();
			bgDesc.entries = entries.data();

			rawBindGroup = m_context.getDevice().createBindGroup(bgDesc);

			if (rawBindGroup)
			{
				spdlog::info("ShaderFactory: Created eager bind group {} with {} buffers (global/static resources)", groupIndex, groupBuffers.size());
			}
			else
			{
				spdlog::error("Failed to create bind group {} with {} buffers", groupIndex, groupBuffers.size());
			}
		}
		else if (hasNonBufferBindings)
		{
			// Lazy creation: This group has textures/samplers, defer to material system
			spdlog::info("ShaderFactory: Created layout-only bind group {} (has textures={}, samplers={}) - lazy init by material", groupIndex, hasTextures, hasSamplers);
		}
		else if (!hasBuffers && !hasNonBufferBindings)
		{
			spdlog::warn("ShaderFactory: Bind group {} has no bindings", groupIndex);
		}

		// Create WebGPUBindGroup wrapper
		// For layout-only groups (hasNonBufferBindings=true), rawBindGroup is nullptr
		// The material system will populate it later via setBindGroup()
		auto bindGroup = std::make_shared<WebGPUBindGroup>(
			rawBindGroup,
			layoutInfo,
			groupBuffers
		);

		m_shaderInfo->addBindGroup(bindGroup);
	}

	// Clear temporary layouts
	m_tempLayouts.clear();
}

void ShaderFactory::loadShaderModule()
{
	if (m_shaderPath.empty())
	{
		spdlog::error("ShaderFactory::loadShaderModule() - No shader path specified");
		return;
	}

	// Use ResourceManager to load shader
	auto shaderModule = engine::resources::ResourceManager::loadShaderModule(
		m_shaderPath,
		m_context.getDevice()
	);

	if (!shaderModule)
	{
		spdlog::error("ShaderFactory::loadShaderModule() - Failed to load shader from '{}'", m_shaderPath.string());
		return;
	}

	m_shaderInfo->setModule(shaderModule);
	spdlog::debug("Loaded shader module from '{}'", m_shaderPath.string());
}

std::shared_ptr<WebGPUBuffer> ShaderFactory::getGlobalBuffer(const std::string &bufferName) const
{
	auto it = m_globalBufferCache.find(bufferName);
	if (it != m_globalBufferCache.end())
	{
		return it->second;
	}
	return nullptr;
}

} // namespace engine::rendering::webgpu
