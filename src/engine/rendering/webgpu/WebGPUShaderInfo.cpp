#include "engine/rendering/webgpu/WebGPUShaderInfo.h"

#include <utility>
#include <optional>

#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"

namespace engine::rendering::webgpu
{

WebGPUShaderInfo::~WebGPUShaderInfo()
{
	if (m_module)
	{
		m_module.release();
	}
	m_module = nullptr;
	m_bindGroupLayouts.clear();
	m_nameToIndex.clear();
	m_typeToIndex.clear();
}

bool WebGPUShaderInfo::isValid() const
{
	return m_module != nullptr && !m_vertexEntryPoint.empty() && !m_fragmentEntryPoint.empty();
}

const std::unordered_map<uint64_t, std::shared_ptr<WebGPUBindGroupLayoutInfo>> &WebGPUShaderInfo::getBindGroupLayouts() const
{
	return m_bindGroupLayouts;
}

std::vector<std::shared_ptr<WebGPUBindGroupLayoutInfo>> WebGPUShaderInfo::getBindGroupLayoutVector() const
{
	std::vector<std::shared_ptr<WebGPUBindGroupLayoutInfo>> layoutVector;
	for (const auto &[_, layout] : m_bindGroupLayouts)
	{
		layoutVector.push_back(layout);
	}
	return layoutVector;
}

std::shared_ptr<WebGPUBindGroupLayoutInfo> WebGPUShaderInfo::getBindGroupLayout(uint32_t groupIndex) const
{
	auto it = m_bindGroupLayouts.find(groupIndex);
	if (it != m_bindGroupLayouts.end())
		return it->second;
	return nullptr;
}

std::shared_ptr<WebGPUBindGroupLayoutInfo> WebGPUShaderInfo::getBindGroupLayout(BindGroupType type) const
{
	auto it = m_typeToIndex.find(type);
	if (it != m_typeToIndex.end())
		return getBindGroupLayout(it->second);
	return nullptr;
}

std::shared_ptr<WebGPUBindGroupLayoutInfo> WebGPUShaderInfo::getBindGroupLayout(const std::string &name) const
{
	auto it = m_nameToIndex.find(name);
	if (it != m_nameToIndex.end())
		return getBindGroupLayout(it->second);
	return nullptr;
}

std::optional<uint64_t> WebGPUShaderInfo::getBindGroupIndex(const std::string &name) const
{
	auto it = m_nameToIndex.find(name);
	if (it != m_nameToIndex.end())
		return it->second;
	return std::nullopt;
}

bool WebGPUShaderInfo::hasBindGroup(const std::string &name) const
{
	return m_nameToIndex.find(name) != m_nameToIndex.end();
}

bool WebGPUShaderInfo::hasBindGroup(BindGroupType type) const
{
	return m_typeToIndex.find(type) != m_typeToIndex.end();
}

void WebGPUShaderInfo::setName(std::string name) { m_name = std::move(name); }
void WebGPUShaderInfo::setPath(std::string path) { m_path = std::move(path); }
void WebGPUShaderInfo::setVertexLayout(engine::rendering::VertexLayout layout) { m_vertexLayout = layout; }
void WebGPUShaderInfo::setVertexEntryPoint(std::string entry) { m_vertexEntryPoint = std::move(entry); }
void WebGPUShaderInfo::setFragmentEntryPoint(std::string entry) { m_fragmentEntryPoint = std::move(entry); }
void WebGPUShaderInfo::setShaderType(engine::rendering::ShaderType type) { m_shaderType = type; }
void WebGPUShaderInfo::setShaderFeatures(engine::rendering::ShaderFeature::Flag features) { m_features = features; }
void WebGPUShaderInfo::setEnableDepth(bool enable) { m_enableDepth = enable; }
void WebGPUShaderInfo::addBindGroupLayout(uint32_t groupIndex, std::shared_ptr<WebGPUBindGroupLayoutInfo> layout)
{
	m_bindGroupLayouts[groupIndex] = std::move(layout);
	auto &ptr = m_bindGroupLayouts[groupIndex];
	m_nameToIndex[ptr->getName()] = groupIndex;
	m_typeToIndex[ptr->getType()] = groupIndex;
}

} // namespace engine::rendering::webgpu
