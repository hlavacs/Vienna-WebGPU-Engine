#include "engine/rendering/webgpu/WebGPUShaderFactory.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <spdlog/spdlog.h>

#include "engine/core/PathProvider.h"
#include "engine/rendering/RenderingConstants.h"
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
	ShaderType type,
	const std::filesystem::path &shaderPath,
	const std::string &vertexEntry,
	const std::string &fragmentEntry,
	engine::rendering::VertexLayout vertexLayout,
	bool depthEnabled,
	bool blendEnabled,
	bool cullBackFaces
)
{
	return WebGPUShaderBuilder(
		*this,
		name,
		type,
		vertexEntry,
		fragmentEntry,
		vertexLayout,
		depthEnabled,
		blendEnabled,
		cullBackFaces,
		shaderPath
	);
}

WebGPUShaderFactory::WebGPUShaderBuilder::WebGPUShaderBuilder(
	WebGPUShaderFactory &factory,
	std::string name,
	ShaderType type,
	std::string vertexEntry,
	std::string fragmentEntry,
	engine::rendering::VertexLayout vertexLayout,
	bool depthEnabled,
	bool blendEnabled,
	bool cullBackFaces,
	std::filesystem::path shaderPath
) : m_factory(factory),
	m_name(std::move(name)),
	m_type(type),
	m_vertexEntry(std::move(vertexEntry)),
	m_fragmentEntry(std::move(fragmentEntry)),
	m_vertexLayout(vertexLayout),
	m_shaderModule(nullptr),
	m_depthEnabled(depthEnabled),
	m_blendEnabled(blendEnabled),
	m_backFaceCullingEnabled(cullBackFaces),
	m_shaderPath(std::move(shaderPath)),
	m_lastBindGroupIndex(-1)
{
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addBindGroup(const std::string &name, BindGroupReuse reuse, BindGroupType type)
{
	uint32_t index = static_cast<uint32_t>(m_bindGroupsBuilder.size());
	BindGroupBuilder group;
	group.name = name;
	group.type = type;
	group.reuse = reuse;
	m_bindGroupsBuilder[index] = std::move(group);
	m_lastBindGroupIndex = index;
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addUniform(
	const std::string &name,
	size_t size,
	uint32_t visibility
)
{
	checkLastBindGroup();
	ShaderBinding b{name, std::nullopt, BindingType::UniformBuffer, static_cast<uint32_t>(-1), size, 0, visibility, false};
	m_bindGroupsBuilder[m_lastBindGroupIndex].bindings.push_back(b);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addStorageBuffer(
	const std::string &name,
	size_t size,
	bool readOnly,
	uint32_t visibility
)
{
	checkLastBindGroup();
	ShaderBinding b{name, std::nullopt, BindingType::StorageBuffer, static_cast<uint32_t>(-1), size, 0, visibility, readOnly};
	m_bindGroupsBuilder[m_lastBindGroupIndex].bindings.push_back(b);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addTexture(
	const std::string &name,
	wgpu::TextureSampleType sampleType,
	wgpu::TextureViewDimension viewDimension,
	bool multisampled,
	uint32_t visibility
)
{
	checkLastBindGroup();
	auto &binding = m_bindGroupsBuilder[m_lastBindGroupIndex].bindings;
	ShaderBinding b{name, std::nullopt, BindingType::Texture, static_cast<uint32_t>(binding.size())};
	b.textureSampleType = sampleType;
	b.textureViewDimension = viewDimension;
	b.textureMultisampled = multisampled;
	b.visibility = visibility;
	binding.push_back(b);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addMaterialTexture(
	const std::string &name,
	const std::string &materialSlotName,
	wgpu::TextureSampleType sampleType,
	wgpu::TextureViewDimension viewDimension,
	uint32_t visibility,
	std::optional<glm::vec3> fallbackColor
)
{
	checkLastBindGroup();
	auto &binding = m_bindGroupsBuilder[m_lastBindGroupIndex].bindings;
	ShaderBinding b{name, materialSlotName, BindingType::MaterialTexture, static_cast<uint32_t>(binding.size())};
	b.textureSampleType = sampleType;
	b.textureViewDimension = viewDimension;
	b.visibility = visibility;
	b.fallbackColor = fallbackColor;
	binding.push_back(b);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addSampler(
	const std::string &name,
	wgpu::SamplerBindingType samplerType,
	uint32_t visibility
)
{
	checkLastBindGroup();
	auto &binding = m_bindGroupsBuilder[m_lastBindGroupIndex].bindings;
	ShaderBinding b{name, "", BindingType::Sampler, static_cast<uint32_t>(binding.size())};
	b.samplerType = samplerType;
	b.visibility = visibility;
	binding.push_back(b);
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addFrameBindGroup()
{
	uint32_t groupIndex = static_cast<uint32_t>(m_bindGroupsBuilder.size());
	auto &bindGroupBuilder = m_bindGroupsBuilder[groupIndex];
	bindGroupBuilder.isEngineDefault = true;
	bindGroupBuilder.name = bindgroup::defaults::FRAME;
	bindGroupBuilder.type = BindGroupType::Frame;
	bindGroupBuilder.reuse = BindGroupReuse::PerFrame;

	ShaderBinding buffer;
	buffer.type = BindingType::UniformBuffer;
	buffer.name = "frameUniforms";
	buffer.binding = 0;
	buffer.size = sizeof(engine::rendering::FrameUniforms);
	buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	buffer.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;

	bindGroupBuilder.bindings.push_back(buffer);
	m_lastBindGroupIndex = groupIndex;
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addObjectBindGroup()
{
	uint32_t groupIndex = static_cast<uint32_t>(m_bindGroupsBuilder.size());
	auto &bindGroupBuilder = m_bindGroupsBuilder[groupIndex];
	bindGroupBuilder.isEngineDefault = true;
	bindGroupBuilder.name = bindgroup::defaults::OBJECT;
	bindGroupBuilder.type = BindGroupType::Object;
	bindGroupBuilder.reuse = BindGroupReuse::PerFrame;

	ShaderBinding buffer;
	buffer.type = BindingType::UniformBuffer;
	buffer.name = "objectUniforms";
	buffer.binding = 0;
	buffer.size = sizeof(engine::rendering::ObjectUniforms);
	buffer.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	buffer.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;

	bindGroupBuilder.bindings.push_back(buffer);
	m_lastBindGroupIndex = groupIndex;
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addLightBindGroup()
{
	uint32_t groupIndex = static_cast<uint32_t>(m_bindGroupsBuilder.size());
	auto &bindGroupBuilder = m_bindGroupsBuilder[groupIndex];
	bindGroupBuilder.isEngineDefault = true;
	bindGroupBuilder.name = bindgroup::defaults::LIGHT;
	bindGroupBuilder.type = BindGroupType::Light;
	bindGroupBuilder.reuse = BindGroupReuse::PerFrame;

	size_t headerSize = sizeof(engine::rendering::LightsBuffer);
	size_t lightArraySize = constants::MAX_LIGHTS * sizeof(engine::rendering::LightStruct);
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
	m_lastBindGroupIndex = groupIndex;
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addShadowBindGroup()
{
	uint32_t groupIndex = static_cast<uint32_t>(m_bindGroupsBuilder.size());
	auto &bindGroupBuilder = m_bindGroupsBuilder[groupIndex];
	bindGroupBuilder.isEngineDefault = true;
	bindGroupBuilder.name = bindgroup::defaults::SHADOW;
	bindGroupBuilder.type = BindGroupType::Shadow;
	bindGroupBuilder.reuse = BindGroupReuse::PerFrame;

	ShaderBinding samplerBinding;
	samplerBinding.type = BindingType::Sampler;
	samplerBinding.name = "shadowSampler";
	samplerBinding.binding = 0;
	samplerBinding.visibility = WGPUShaderStage_Fragment;
	samplerBinding.samplerType = wgpu::SamplerBindingType::Comparison;
	bindGroupBuilder.bindings.push_back(samplerBinding);

	ShaderBinding shadowMaps2D;
	shadowMaps2D.type = BindingType::Texture;
	shadowMaps2D.name = "shadowMaps2D";
	shadowMaps2D.binding = 1;
	shadowMaps2D.visibility = WGPUShaderStage_Fragment;
	shadowMaps2D.textureSampleType = wgpu::TextureSampleType::Depth;
	shadowMaps2D.textureViewDimension = wgpu::TextureViewDimension::_2DArray;
	shadowMaps2D.textureMultisampled = false;
	bindGroupBuilder.bindings.push_back(shadowMaps2D);

	ShaderBinding shadowMapsCube;
	shadowMapsCube.type = BindingType::Texture;
	shadowMapsCube.name = "shadowMapsCube";
	shadowMapsCube.binding = 2;
	shadowMapsCube.visibility = WGPUShaderStage_Fragment;
	shadowMapsCube.textureSampleType = wgpu::TextureSampleType::Depth;
	shadowMapsCube.textureViewDimension = wgpu::TextureViewDimension::CubeArray;
	shadowMapsCube.textureMultisampled = false;
	bindGroupBuilder.bindings.push_back(shadowMapsCube);

	ShaderBinding shadowUniformBuffer;
	shadowUniformBuffer.type = BindingType::StorageBuffer;
	shadowUniformBuffer.name = "uShadows";
	shadowUniformBuffer.binding = 3;
	size_t maxUnifiedShadows = constants::MAX_SHADOW_MAPS_2D + constants::MAX_SHADOW_MAPS_CUBE;
	shadowUniformBuffer.size = maxUnifiedShadows * sizeof(engine::rendering::ShadowUniform);
	shadowUniformBuffer.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	shadowUniformBuffer.visibility = WGPUShaderStage_Fragment;
	shadowUniformBuffer.readOnly = true;
	bindGroupBuilder.bindings.push_back(shadowUniformBuffer);

	m_lastBindGroupIndex = groupIndex;
	return *this;
}

WebGPUShaderFactory::WebGPUShaderBuilder &WebGPUShaderFactory::WebGPUShaderBuilder::addCustomUniform(
	const std::string &name,
	size_t size,
	uint32_t visibility
)
{
	checkLastBindGroup();
	auto &binding = m_bindGroupsBuilder[m_lastBindGroupIndex].bindings;

	ShaderBinding b{name, std::nullopt, BindingType::UniformBuffer, static_cast<uint32_t>(-1), size, 0, visibility, false};

	binding.push_back(b);
	return *this;
}

void WebGPUShaderFactory::WebGPUShaderBuilder::checkLastBindGroup()
{
	if (m_lastBindGroupIndex < 0)
		throw std::runtime_error("No bind group added! Add a bind group before adding bindings.");
}

std::shared_ptr<WebGPUShaderInfo> WebGPUShaderFactory::WebGPUShaderBuilder::build()
{
	wgpu::ShaderModule shaderModule = m_shaderModule;
	if (!shaderModule && !m_shaderPath.empty())
	{
		shaderModule = m_factory.loadShaderModule(m_shaderPath.string());
		if (!shaderModule)
		{
			spdlog::error("WebGPUShaderFactory::build() - Failed to load shader from '{}'", m_shaderPath.string());
			return nullptr;
		}
	}

	if (!shaderModule)
	{
		spdlog::error("WebGPUShaderFactory::build() - No shader module set for shader '{}'", m_name);
		return nullptr;
	}

	auto builtShaderInfo = std::make_shared<WebGPUShaderInfo>(
		m_name,
		m_shaderPath,
		m_type,
		shaderModule,
		m_vertexEntry,
		m_fragmentEntry,
		m_vertexLayout,
		engine::rendering::ShaderFeature::Flag(m_shaderFeatures),
		m_depthEnabled,
		m_blendEnabled,
		m_backFaceCullingEnabled
	);

	m_factory.createBindGroupLayouts(builtShaderInfo, m_bindGroupsBuilder);

	spdlog::info("WebGPUShaderFactory: Built shader '{}' with {} bind groups", builtShaderInfo->getName(), builtShaderInfo->getBindGroupLayouts().size());

	return builtShaderInfo;
}

void WebGPUShaderFactory::createBindGroupLayouts(
	std::shared_ptr<WebGPUShaderInfo> shaderInfo,
	std::map<uint32_t, BindGroupBuilder> &bindGroupsBuilder
)
{
	for (auto &[groupIndex, bindGroupBuilder] : bindGroupsBuilder)
	{
		if (bindGroupBuilder.reuse == BindGroupReuse::Global || bindGroupBuilder.isEngineDefault)
		{
			auto existingLayout = m_context.bindGroupFactory().getGlobalBindGroupLayout(bindGroupBuilder.name);
			if (existingLayout)
			{
				shaderInfo->addBindGroupLayout(groupIndex, existingLayout);
				spdlog::debug("Reused existing global bind group layout for group {} with key '{}'", groupIndex, bindGroupBuilder.name);
				continue;
			}
		}
		// Create layout entries from bindings
		std::vector<wgpu::BindGroupLayoutEntry> entries;
		std::vector<BindGroupBinding> typedBindings;
		auto bindings = bindGroupBuilder.bindings;
		entries.reserve(bindings.size());
		typedBindings.reserve(bindings.size());

		auto bindingIndex = 0u;
		for (const auto &binding : bindings)
		{
			wgpu::BindGroupLayoutEntry entry{};
			entry.binding = binding.binding == static_cast<uint32_t>(-1) ? bindingIndex++ : binding.binding;
			bindingIndex = std::max<uint32_t>(bindingIndex, static_cast<uint32_t>(entry.binding + 1u));
			entry.visibility = binding.visibility;

			// Configure entry based on binding type using WebGPUBindGroupFactory helpers
			switch (binding.type)
			{
			case BindingType::UniformBuffer:
				entry.buffer.type = wgpu::BufferBindingType::Uniform;
				entry.buffer.minBindingSize = binding.size;
				entry.buffer.hasDynamicOffset = false;
				break;

			case BindingType::StorageBuffer:
				entry.buffer.type = binding.readOnly ? wgpu::BufferBindingType::ReadOnlyStorage
													 : wgpu::BufferBindingType::Storage;
				entry.buffer.hasDynamicOffset = false;
				entry.buffer.minBindingSize = binding.size;
				break;

			case BindingType::Texture:
				entry.texture.sampleType = binding.textureSampleType;
				entry.texture.viewDimension = binding.textureViewDimension;
				entry.texture.multisampled = binding.textureMultisampled;
				break;

			case BindingType::MaterialTexture:
				entry.texture.sampleType = binding.textureSampleType;
				entry.texture.viewDimension = binding.textureViewDimension;
				entry.texture.multisampled = false;
				break;

			case BindingType::Sampler:
				entry.sampler.type = binding.samplerType;
				break;
			}

			entries.push_back(entry);

			// Create typed binding metadata for WebGPUBindGroupLayoutInfo
			BindGroupBinding typedBinding{entry.binding, binding.name};
			typedBinding.visibility = static_cast<WGPUShaderStage>(entry.visibility);
			typedBinding.type = binding.type;
			if (binding.type == BindingType::UniformBuffer || binding.type == BindingType::StorageBuffer)
			{
				typedBinding.size = entry.buffer.minBindingSize;
			}
			else if (binding.type == BindingType::MaterialTexture)
			{
				typedBinding.materialSlotName = binding.materialSlotName;
				typedBinding.fallbackColor = binding.fallbackColor;
			}

			typedBindings.push_back(typedBinding);
		}

		// Create the bind group layout descriptor
		wgpu::BindGroupLayoutDescriptor layoutDesc{};
		layoutDesc.entryCount = entries.size();
		layoutDesc.entries = entries.data();

		auto layoutInfo = m_context.bindGroupFactory().createBindGroupLayoutInfo(
			bindGroupBuilder.name,
			bindGroupBuilder.type,
			bindGroupBuilder.reuse,
			entries,
			typedBindings
		);

		if (bindGroupBuilder.reuse == BindGroupReuse::Global || bindGroupBuilder.isEngineDefault)
		{
			m_context.bindGroupFactory().storeGlobalBindGroupLayout(bindGroupBuilder.name, layoutInfo);
		}

		shaderInfo->addBindGroupLayout(groupIndex, layoutInfo);

		spdlog::debug("Created bind group layout '{}' for group {} with {} entries", layoutInfo->getName(), groupIndex, entries.size());
	}
}

wgpu::ShaderModule WebGPUShaderFactory::loadShaderModule(const std::filesystem::path &shaderPath)
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
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - Failed to open shader file '{}'", shaderPath.string());
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
		spdlog::error("WebGPUShaderFactory::loadShaderModule() - Failed to load shader from '{}'", shaderPath.string());
		return nullptr;
	}
	return shaderModule;
}

bool WebGPUShaderFactory::reloadShader(std::shared_ptr<WebGPUShaderInfo> shaderInfo)
{
	if (!shaderInfo || shaderInfo->getPath().empty())
	{
		spdlog::error("WebGPUShaderFactory::reloadShader() - Invalid shader info or has no path");
		return nullptr;
	}

	std::filesystem::path shaderPath = shaderInfo->getPath();
	auto shaderModule = loadShaderModule(shaderPath);

	if (!shaderModule)
	{
		spdlog::error("WebGPUShaderFactory::reloadShader() - Failed to reload shader module from '{}'", shaderPath.string());
		return nullptr;
	}

	// Create a new immutable WebGPUShaderInfo with the reloaded module
	auto newShaderInfo = std::make_shared<WebGPUShaderInfo>(
		shaderInfo->getName(),
		shaderInfo->getPath(),
		shaderInfo->getShaderType(),
		shaderModule,
		shaderInfo->getVertexEntryPoint(),
		shaderInfo->getFragmentEntryPoint(),
		shaderInfo->getVertexLayout(),
		shaderInfo->getShaderFeatures(),
		shaderInfo->isDepthEnabled(),
		shaderInfo->isBlendEnabled(),
		shaderInfo->isBackFaceCullingEnabled()
	);

	// Copy bind group layouts from old shader to new one
	for (const auto &[groupIndex, layoutInfo] : shaderInfo->getBindGroupLayouts())
	{
		newShaderInfo->addBindGroupLayout(groupIndex, layoutInfo);
	}

	return m_context.shaderRegistry().registerShader(newShaderInfo, true);
}

} // namespace engine::rendering::webgpu
