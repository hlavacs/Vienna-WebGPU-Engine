#include "engine/rendering/webgpu/WebGPUShaderFactory.h"

#include <cstring>
#include <fstream>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/FrameUniforms.h"
#include "engine/rendering/LightUniforms.h"
#include "engine/rendering/ObjectUniforms.h"
#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUShaderFactory::WebGPUShaderFactory(WebGPUContext &context) : m_context(context)
{
}

WebGPUShaderFactory::WebGPUShaderBuilder WebGPUShaderFactory::begin(
	const std::string &name,
	const ShaderType type,
	const std::string &vertexEntry,
	const std::string &fragmentEntry,
	const std::optional<std::filesystem::path> &shaderPath
)
{
	// Reset state for new shader
	auto shaderInfo = std::make_shared<WebGPUShaderInfo>(
		name,
		shaderPath ? shaderPath->string() : "",
		type,
		vertexEntry,
		fragmentEntry
	);

	return WebGPUShaderFactory::WebGPUShaderBuilder(*this, shaderInfo);
}

void WebGPUShaderFactory::reloadShader(const std::shared_ptr<WebGPUShaderInfo> &shaderInfo)
{
	std::string shaderPath = shaderInfo->getPath();

	auto shaderModule = loadShaderModule(shaderPath);

	if (!shaderModule)
	{
		spdlog::error("WebGPUShaderFactory::reloadShader() - Failed to reload shader from '{}'", shaderPath);
		return;
	}

	shaderInfo->setModule(shaderModule);
}

WebGPUShaderFactory::BindGroupBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::getOrCreateBindGroup(uint32_t groupIndex)
{
	return m_bindGroupsBuilder[groupIndex];
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::setShaderModule(wgpu::ShaderModule module)
{
	m_shaderInfo->setModule(module);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::setVertexLayout(engine::rendering::VertexLayout layout)
{
	m_shaderInfo->setVertexLayout(layout);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::disableDepth()
{
	m_shaderInfo->setEnableDepth(false);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addFrameUniforms(uint32_t groupIndex)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);
	bindGroupBuilder.key = "frameUniforms";
	bindGroupBuilder.isGlobal = true; // frame uniforms are global

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

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addLightUniforms(uint32_t groupIndex, size_t maxLights)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);
	bindGroupBuilder.key = "lightUniforms";
	bindGroupBuilder.isGlobal = true; // light uniforms are global

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

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addShadowUniforms(uint32_t groupIndex, size_t maxShadows, size_t maxShadowCubes)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);
	bindGroupBuilder.key = "shadowMaps";
	bindGroupBuilder.isGlobal = true; // shadows are global

	// Binding 0: Shadow comparison sampler
	ShaderBinding samplerBinding;
	samplerBinding.type = BindingType::Sampler;
	samplerBinding.name = "shadowSampler";
	samplerBinding.binding = 0;
	samplerBinding.visibility = WGPUShaderStage_Fragment;
	samplerBinding.samplerType = wgpu::SamplerBindingType::Comparison;
	bindGroupBuilder.bindings.push_back(samplerBinding);

	// Binding 1: 2D shadow map array (for directional & spot lights)
	ShaderBinding shadowMaps2D;
	shadowMaps2D.type = BindingType::Texture;
	shadowMaps2D.name = "shadowMaps2D";
	shadowMaps2D.binding = 1;
	shadowMaps2D.visibility = WGPUShaderStage_Fragment;
	shadowMaps2D.textureSampleType = wgpu::TextureSampleType::Depth;
	shadowMaps2D.textureViewDimension = wgpu::TextureViewDimension::_2DArray;
	shadowMaps2D.textureMultisampled = false;
	bindGroupBuilder.bindings.push_back(shadowMaps2D);

	// Binding 2: Cube shadow map array (for point lights)
	ShaderBinding shadowMapsCube;
	shadowMapsCube.type = BindingType::Texture;
	shadowMapsCube.name = "shadowMapsCube";
	shadowMapsCube.binding = 2;
	shadowMapsCube.visibility = WGPUShaderStage_Fragment;
	shadowMapsCube.textureSampleType = wgpu::TextureSampleType::Depth;
	shadowMapsCube.textureViewDimension = wgpu::TextureViewDimension::CubeArray;
	shadowMapsCube.textureMultisampled = false;
	bindGroupBuilder.bindings.push_back(shadowMapsCube);

	// Binding 3: Shadow2D data storage buffer
	ShaderBinding shadow2DBuffer;
	shadow2DBuffer.type = BindingType::StorageBuffer;
	shadow2DBuffer.name = "uShadow2D";
	shadow2DBuffer.binding = 3;
	shadow2DBuffer.size = maxShadows * sizeof(engine::rendering::Shadow2D);
	shadow2DBuffer.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	shadow2DBuffer.visibility = WGPUShaderStage_Fragment;
	shadow2DBuffer.readOnly = true;
	bindGroupBuilder.bindings.push_back(shadow2DBuffer);

	// Binding 4: ShadowCube data storage buffer
	ShaderBinding shadowCubeBuffer;
	shadowCubeBuffer.type = BindingType::StorageBuffer;
	shadowCubeBuffer.name = "uShadowCube";
	shadowCubeBuffer.binding = 4;
	shadowCubeBuffer.size = maxShadowCubes * sizeof(engine::rendering::ShadowCube);
	shadowCubeBuffer.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	shadowCubeBuffer.visibility = WGPUShaderStage_Fragment;
	shadowCubeBuffer.readOnly = true;
	bindGroupBuilder.bindings.push_back(shadowCubeBuffer);

	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addObjectUniforms(uint32_t groupIndex)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);
	bindGroupBuilder.key = "objectUniforms";

	ShaderBinding buffer;
	buffer.type = BindingType::UniformBuffer;
	buffer.name = "objectUniforms";
	buffer.binding = 0;
	buffer.size = sizeof(engine::rendering::ObjectUniforms);
	buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	buffer.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;

	bindGroupBuilder.bindings.push_back(buffer);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addCustomUniform(
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

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::asGlobalBindGroup(uint32_t groupIndex, std::string key)
{
	auto &bindGroupBuilder = getOrCreateBindGroup(groupIndex);
	bindGroupBuilder.key = std::move(key);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addStorageBuffer(
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

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addTexture(
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

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addSampler(
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

std::shared_ptr<WebGPUShaderInfo> WebGPUShaderFactory::WebGPUShaderBuilder::build()
{
	// Load shader module if not already set and path provided
	if (!m_shaderInfo->getModule() && !m_shaderInfo->getPath().empty())
	{
		auto shaderModule = m_factory.loadShaderModule(m_shaderInfo->getPath());
		if (!shaderModule)
		{
			return nullptr;
		}
		m_shaderInfo->setModule(shaderModule);
	}

	// Validate shader module exists
	if (!m_shaderInfo->getModule())
	{
		spdlog::error("WebGPUShaderFactory::build() - No shader module set for shader '{}'", m_shaderInfo->getName());
		return nullptr;
	}

	m_factory.createBindGroupLayouts(m_shaderInfo, m_bindGroupsBuilder);

	spdlog::info("WebGPUShaderFactory: Built shader '{}' with {} bind groups", m_shaderInfo->getName(), m_shaderInfo->getBindGroupLayouts().size());

	return m_shaderInfo;
}

void WebGPUShaderFactory::createBindGroupLayouts(
	std::shared_ptr<WebGPUShaderInfo> shaderInfo,
	std::map<uint32_t, BindGroupBuilder> &bindGroupsBuilder
)
{
	for (auto &[groupIndex, bindGroupBuilder] : bindGroupsBuilder)
	{
		if (bindGroupBuilder.key.has_value())
		{
			auto existingLayout = m_context.bindGroupFactory().getGlobalBindGroupLayout(bindGroupBuilder.key.value());
			if (existingLayout)
			{
				shaderInfo->addBindGroupLayout(groupIndex, existingLayout);
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
			shaderInfo->getName() + "_BindGroupLayout_" + std::to_string(groupIndex),
			entries
		);
		if (bindGroupBuilder.key.has_value())
		{
			layoutInfo->setKey(bindGroupBuilder.key);
			layoutInfo->setGlobal(bindGroupBuilder.isGlobal);
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

		shaderInfo->addBindGroupLayout(groupIndex, layoutInfo);

		spdlog::debug("Created bind group layout {} for group {} with {} entries", groupIndex, groupIndex, entries.size());
	}
}

wgpu::ShaderModule WebGPUShaderFactory::loadShaderModule(const std::string &shaderPath)
{
	if (shaderPath.empty())
	{
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - No shader path specified");
		return nullptr;
	}

	// Use ResourceManager to load shader
	std::ifstream file(shaderPath);
	if (!file.is_open())
	{
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - Failed to open shader file '{}'", shaderPath);
		return nullptr;
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
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - Failed to load shader from '{}'", shaderPath);
		return nullptr;
	}
	return shaderModule;
}

} // namespace engine::rendering::webgpu
