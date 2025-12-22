#include "engine/rendering/webgpu/WebGPUShaderFactory.h"

#include <cstring>
#include <fstream>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUShaderFactory::WebGPUShaderFactory(WebGPUContext &context) : m_context(context), m_shaderInfo(nullptr)
{
}

WebGPUShaderFactory &WebGPUShaderFactory::begin(
	const std::string &name,
	const std::string &vertexEntry,
	const std::string &fragmentEntry,
	const std::filesystem::path &shaderPath
)
{
	// Reset state for new shader
	m_shaderInfo = std::make_shared<WebGPUShaderInfo>(
		name,
		shaderPath.string(),
		vertexEntry,
		fragmentEntry
	);
	m_shaderPath = shaderPath;
	m_bindGroupsBuilder.clear();

	return *this;
}

WebGPUShaderFactory &WebGPUShaderFactory::setShaderModule(wgpu::ShaderModule module)
{
	m_shaderInfo->setModule(module);
	return *this;
}

WebGPUShaderFactory &WebGPUShaderFactory::addFrameUniforms(uint32_t groupIndex)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);
	bindGroupBuilder.key = "frameUniforms";

	ShaderBinding buffer;
	buffer.type = BindingType::UniformBuffer;
	buffer.name = "frameUniforms";
	buffer.binding = 0;
	buffer.size = sizeof(engine::rendering::FrameUniforms);
	buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	buffer.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;

	bindGroupBuilder.bindings.push_back(buffer);
	return *this;
}

WebGPUShaderFactory &WebGPUShaderFactory::addLightUniforms(uint32_t groupIndex, size_t maxLights)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);
	bindGroupBuilder.key = "lightUniforms";

	// Calculate buffer size: header + light array
	size_t headerSize = sizeof(engine::rendering::LightsBuffer);
	size_t lightArraySize = maxLights * sizeof(engine::rendering::LightStruct);
	size_t totalSize = headerSize + lightArraySize;

	ShaderBinding buffer;
	buffer.type = BindingType::StorageBuffer;
	buffer.name = "lightUniforms";
	buffer.binding = 0;
	buffer.size = totalSize;
	buffer.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	buffer.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	buffer.readOnly = true;

	bindGroupBuilder.bindings.push_back(buffer);
	return *this;
}

WebGPUShaderFactory &WebGPUShaderFactory::addCustomUniform(
	const std::string &name,
	size_t size,
	uint32_t groupIndex,
	uint32_t binding,
	uint32_t visibility
)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);

	ShaderBinding buffer;
	buffer.type = BindingType::UniformBuffer;
	buffer.name = name;
	buffer.binding = binding;
	buffer.size = size;
	buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	buffer.visibility = visibility;

	bindGroupBuilder.bindings.push_back(buffer);
	return *this;
}

WebGPUShaderFactory &WebGPUShaderFactory::asGlobalBindGroup(uint32_t groupIndex, std::string key)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);
	bindGroupBuilder.key = std::move(key);
	return *this;
}

WebGPUShaderFactory &WebGPUShaderFactory::addStorageBuffer(
	const std::string &name,
	size_t size,
	uint32_t groupIndex,
	uint32_t binding,
	bool readOnly,
	uint32_t visibility
)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);

	ShaderBinding buffer;
	buffer.type = BindingType::StorageBuffer;
	buffer.name = name;
	buffer.binding = binding;
	buffer.size = size;
	buffer.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	buffer.visibility = visibility;
	buffer.readOnly = readOnly;

	bindGroupBuilder.bindings.push_back(buffer);
	return *this;
}

WebGPUShaderFactory &WebGPUShaderFactory::addTexture(
	const std::string &name,
	const std::string &materialSlotName,
	uint32_t groupIndex,
	uint32_t binding,
	wgpu::TextureSampleType sampleType,
	wgpu::TextureViewDimension viewDimension,
	bool multisampled,
	uint32_t visibility,
	std::optional<glm::vec3> fallbackColor
)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);

	ShaderBinding textureBinding;
	textureBinding.type = BindingType::Texture;
	textureBinding.name = name;
	textureBinding.materialSlotName = materialSlotName;
	textureBinding.binding = binding;
	textureBinding.visibility = visibility;
	textureBinding.textureSampleType = sampleType;
	textureBinding.textureViewDimension = viewDimension;
	textureBinding.textureMultisampled = multisampled;
	textureBinding.fallbackColor = fallbackColor;

	bindGroupBuilder.bindings.push_back(textureBinding);
	return *this;
}

WebGPUShaderFactory &WebGPUShaderFactory::addSampler(
	const std::string &name,
	uint32_t groupIndex,
	uint32_t binding,
	wgpu::SamplerBindingType samplerType,
	uint32_t visibility
)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);

	ShaderBinding samplerBinding;
	samplerBinding.type = BindingType::Sampler;
	samplerBinding.name = name;
	samplerBinding.binding = binding;
	samplerBinding.visibility = visibility;
	samplerBinding.samplerType = samplerType;

	bindGroupBuilder.bindings.push_back(samplerBinding);
	return *this;
}

void WebGPUShaderFactory::reloadShader(const std::shared_ptr<WebGPUShaderInfo> &shaderInfo)
{
	std::string shaderPath = shaderInfo->getPath();

	std::ifstream file(shaderPath);
	if (!file.is_open())
	{
		spdlog::error("WebGPUShaderFactory::reloadShader() - Failed to open shader file '{}'", shaderPath);
		return;
	}
	file.seekg(0, std::ios::end);
	size_t size = file.tellg();
	std::string shaderSource(size, ' ');
	file.seekg(0);
	file.read(shaderSource.data(), size);

	wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
	shaderCodeDesc.chain.next = nullptr;
	shaderCodeDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
	shaderCodeDesc.code = shaderSource.c_str();
	wgpu::ShaderModuleDescriptor shaderDesc;
	shaderDesc.nextInChain = &shaderCodeDesc.chain;
#ifdef WEBGPU_BACKEND_WGPU
	shaderDesc.hintCount = 0;
	shaderDesc.hints = nullptr;
#endif

	auto shaderModule = m_context.getDevice().createShaderModule(shaderDesc);

	if (!shaderModule)
	{
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - Failed to load shader from '{}'", m_shaderPath.string());
		return;
	}

	m_shaderInfo->setModule(shaderModule);
	spdlog::debug("Loaded shader module from '{}'", m_shaderPath.string());
}

WebGPUShaderFactory::BindGroupBuilder &WebGPUShaderFactory::getOrCreateBindGroup(uint32_t groupIndex)
{
	return m_bindGroupsBuilder[groupIndex];
}

std::shared_ptr<WebGPUShaderInfo> WebGPUShaderFactory::build()
{
	// Load shader module if not already set and path provided
	if (!m_shaderInfo->getModule() && !m_shaderPath.empty())
	{
		loadShaderModule();
	}

	// Validate shader module exists
	if (!m_shaderInfo->getModule())
	{
		spdlog::error("WebGPUShaderFactory::build() - No shader module set for shader '{}'", m_shaderInfo->getName());
		return nullptr;
	}

	createBindGroupLayouts();

	spdlog::info("WebGPUShaderFactory: Built shader '{}' with {} bind groups", m_shaderInfo->getName(), m_shaderInfo->getBindGroupLayouts().size());

	return m_shaderInfo;
}

void WebGPUShaderFactory::createBindGroupLayouts()
{
	for (auto &[groupIndex, bindGroupBuilder] : m_bindGroupsBuilder)
	{
		if (bindGroupBuilder.key.has_value())
		{
			auto existingLayout = m_context.bindGroupFactory().getGlobalBindGroupLayout(bindGroupBuilder.key.value());
			if (existingLayout)
			{
				m_shaderInfo->addBindGroupLayout(groupIndex, existingLayout);
				spdlog::debug("Reused existing global bind group layout for group {} with key '{}'", groupIndex, bindGroupBuilder.key.value());
				continue;
			}
		}
		// Create layout entries from bindings
		std::vector<wgpu::BindGroupLayoutEntry> entries;
		auto bindings = bindGroupBuilder.bindings;
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

		auto layoutInfo = m_context.bindGroupFactory().createBindGroupLayoutInfo(
			m_shaderInfo->getName() + "_BindGroupLayout_" + std::to_string(groupIndex),
			entries
		);
		if (bindGroupBuilder.key.has_value())
		{
			layoutInfo->setKey(bindGroupBuilder.key);
			m_context.bindGroupFactory().storeGlobalBindGroupLayout(bindGroupBuilder.key.value(), layoutInfo);
		}

		// Store material slot names for texture bindings
		for (const auto &binding : bindings)
		{
			if (binding.type != BindingType::Texture)
				continue;
			if (!binding.materialSlotName.empty())
			{
				layoutInfo->setMaterialSlotName(binding.binding, binding.materialSlotName);
			}
			layoutInfo->setFallbackColor(binding.binding, binding.fallbackColor);
		}

		m_shaderInfo->addBindGroupLayout(groupIndex, layoutInfo);

		spdlog::debug("Created bind group layout {} for group {} with {} entries", groupIndex, groupIndex, entries.size());
	}
}

void WebGPUShaderFactory::loadShaderModule()
{
	if (m_shaderPath.empty())
	{
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - No shader path specified");
		return;
	}

	// Use ResourceManager to load shader
	std::ifstream file(m_shaderPath);
	if (!file.is_open())
	{
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - Failed to open shader file '{}'", m_shaderPath.string());
		return;
	}
	file.seekg(0, std::ios::end);
	size_t size = file.tellg();
	std::string shaderSource(size, ' ');
	file.seekg(0);
	file.read(shaderSource.data(), size);

	wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc;
	shaderCodeDesc.chain.next = nullptr;
	shaderCodeDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
	shaderCodeDesc.code = shaderSource.c_str();
	wgpu::ShaderModuleDescriptor shaderDesc;
	shaderDesc.nextInChain = &shaderCodeDesc.chain;
#ifdef WEBGPU_BACKEND_WGPU
	shaderDesc.hintCount = 0;
	shaderDesc.hints = nullptr;
#endif

	auto shaderModule = m_context.getDevice().createShaderModule(shaderDesc);

	if (!shaderModule)
	{
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - Failed to load shader from '{}'", m_shaderPath.string());
		return;
	}

	m_shaderInfo->setModule(shaderModule);
	spdlog::debug("Loaded shader module from '{}'", m_shaderPath.string());
}

} // namespace engine::rendering::webgpu
