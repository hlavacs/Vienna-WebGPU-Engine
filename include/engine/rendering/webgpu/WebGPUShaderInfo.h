#pragma once

#include "engine/rendering/ShaderFeatureMask.h"
#include "engine/rendering/ShaderType.h"
#include "engine/rendering/Vertex.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

class WebGPUBindGroupLayoutInfo;
class WebGPUShaderFactory;

/**
 * @brief Pure shader metadata with manual reflection information.
 *
 * Contains:
 * - Shader module and entry points
 * - Bind group layouts
 *
 * No GPU buffers or bind groups are stored here.
 */
class WebGPUShaderInfo
{
	friend class WebGPUShaderFactory;

  public:
	// Constructors
	WebGPUShaderInfo(
		std::string name,
		std::string path,
		engine::rendering::ShaderType type,
		std::string vertexEntry,
		std::string fragmentEntry,
		engine::rendering::VertexLayout vertexLayout = engine::rendering::VertexLayout::PositionNormalUVTangentColor,
		engine::rendering::ShaderFeature features = engine::rendering::ShaderFeature::None,
		bool enableDepth = true,
		bool enableBlend = false,
		bool cullBackFaces = true
	) : m_name(std::move(name)),
		m_path(std::move(path)),
		m_shaderType(type),
		m_vertexEntryPoint(std::move(vertexEntry)),
		m_fragmentEntryPoint(std::move(fragmentEntry)),
		m_vertexLayout(vertexLayout),
		m_features(features),
		m_enableDepth(enableDepth),
		m_enableBlend(enableBlend),
		m_cullBackFaces(cullBackFaces)
	{
	}

	WebGPUShaderInfo(
		std::string name,
		std::string path,
		engine::rendering::ShaderType type,
		wgpu::ShaderModule mod,
		std::string vertexEntry,
		std::string fragmentEntry,
		engine::rendering::VertexLayout vertexLayout = engine::rendering::VertexLayout::PositionNormalUVTangentColor,
		engine::rendering::ShaderFeature features = engine::rendering::ShaderFeature::None,
		bool enableDepth = true,
		bool enableBlend = false,
		bool cullBackFaces = true
	) : m_name(std::move(name)),
		m_path(std::move(path)),
		m_shaderType(type),
		m_module(mod),
		m_vertexEntryPoint(std::move(vertexEntry)),
		m_fragmentEntryPoint(std::move(fragmentEntry)),
		m_vertexLayout(vertexLayout),
		m_features(features),
		m_enableDepth(enableDepth),
		m_enableBlend(enableBlend),
		m_cullBackFaces(cullBackFaces)
	{
	}
	/**
	 * @brief Gets the shader name.
	 * @return Shader name string.
	 */
	const std::string &getName() const { return m_name; }
	/**
	 * @brief Gets the shader file path.
	 * @return Shader file path string.
	 */
	const std::string &getPath() const { return m_path; }
	/**
	 * @brief Gets the WebGPU shader module.
	 * @return The WebGPU shader module.
	 */
	wgpu::ShaderModule getModule() const { return m_module; }
	/**
	 * @brief Gets the vertex entry point name.
	 * @return Vertex entry point string.
	 */
	const std::string &getVertexEntryPoint() const { return m_vertexEntryPoint; }
	/**
	 * @brief Gets the fragment entry point name.
	 * @return Fragment entry point string.
	 */
	const std::string &getFragmentEntryPoint() const { return m_fragmentEntryPoint; }
	/**
	 * @brief Gets the shader type.
	 * @return Shader type enum.
	 */
	const engine::rendering::ShaderType &getShaderType() const { return m_shaderType; }
	/**
	 * @brief Gets the vertex layout.
	 * @return Vertex layout enum.
	 */
	const engine::rendering::VertexLayout &getVertexLayout() const { return m_vertexLayout; }
	/**
	 * @brief Gets the vertex layout.
	 * @return Vertex layout enum.
	 */
	const engine::rendering::ShaderFeature &getShaderFeatures() const { return m_features; }
	/**
	 * @brief Wether depth testing is enabled.
	 * @return True if depth testing is enabled.
	 */
	bool isDepthEnabled() const { return m_enableDepth; }
	/**
	 * @brief Wether blending is enabled.
	 * @return True if blending is enabled.
	 */
	bool isBlendEnabled() const { return m_enableBlend; }
	/**
	 * @brief Wether back-face culling is enabled.
	 * @return True if back-face culling is enabled.
	 */
	bool isBackFaceCullingEnabled() const { return m_cullBackFaces; }

	/**
	 * @brief Access bind group layouts for pipeline creation.
	 * @return Map of group index to layout info.
	 */
	const std::unordered_map<uint32_t, std::shared_ptr<WebGPUBindGroupLayoutInfo>> &getBindGroupLayouts() const
	{
		return m_bindGroupLayouts;
	}

	/**
	 * @brief Access bind group layouts for pipeline creation.
	 * @return Vector of layout infos.
	 */
	const std::vector<std::shared_ptr<WebGPUBindGroupLayoutInfo>> &getBindGroupLayoutVector() const
	{
		static std::vector<std::shared_ptr<WebGPUBindGroupLayoutInfo>> layoutVector;
		layoutVector.clear();
		for (const auto &[_, layout] : m_bindGroupLayouts)
		{
			layoutVector.push_back(layout);
		}
		return layoutVector;
	}

	/**
	 * @brief Get a specific bind group layout by index.
	 * @param groupIndex The bind group index.
	 * @return Shared pointer to layout info or nullptr.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> getBindGroupLayout(uint32_t groupIndex) const
	{
		auto it = m_bindGroupLayouts.find(groupIndex);
		if (it != m_bindGroupLayouts.end())
			return it->second;
		return nullptr;
	}

	/**
	 * @brief Check if shader info is valid.
	 * @return True if valid, false otherwise.
	 */
	bool isValid() const
	{
		return m_module != nullptr && !m_vertexEntryPoint.empty() && !m_fragmentEntryPoint.empty();
	}

  private:
	/**
	 * @brief Set the shader name.
	 * @param name The shader name.
	 */
	void setName(std::string name) { m_name = std::move(name); }

	/**
	 * @brief Set the shader path.
	 * @param path The shader file path.
	 */
	void setPath(std::string path) { m_path = std::move(path); }

	/**
	 * @brief Set the shader module.
	 * @param mod The WebGPU shader module.
	 */
	void setModule(wgpu::ShaderModule mod) { m_module = mod; }

	/**
	 * @brief Set the vertex entry point name.
	 * @param entry The vertex entry point name.
	 */
	void setVertexEntryPoint(std::string entry) { m_vertexEntryPoint = std::move(entry); }

	/**
	 * @brief Set the fragment entry point name.
	 * @param entry The fragment entry point name.
	 */
	void setFragmentEntryPoint(std::string entry) { m_fragmentEntryPoint = std::move(entry); }

	/**
	 * @brief Set the shader type.
	 * @param type The shader type.
	 */
	void setShaderType(engine::rendering::ShaderType type) { m_shaderType = type; }

	/**
	 * @brief Set the shader features.
	 * @param features The shader feature mask.
	 */
	void setShaderFeatures(engine::rendering::ShaderFeature features) { m_features = features; }

	/**
	 * @brief Add a bind group layout at the specified index.
	 * @param groupIndex The bind group index.
	 */
	void addBindGroupLayout(uint32_t groupIndex, std::shared_ptr<WebGPUBindGroupLayoutInfo> layout) { m_bindGroupLayouts[groupIndex] = std::move(layout); }

	bool m_enableDepth;
	bool m_enableBlend;
	bool m_cullBackFaces;

	std::string m_name;
	std::string m_path;
	wgpu::ShaderModule m_module = nullptr;
	std::string m_vertexEntryPoint;
	std::string m_fragmentEntryPoint;
	engine::rendering::ShaderType m_shaderType = engine::rendering::ShaderType::Lit;
	engine::rendering::VertexLayout m_vertexLayout = engine::rendering::VertexLayout::PositionNormalUVTangentColor;
	engine::rendering::ShaderFeature m_features = engine::rendering::ShaderFeature::None;

	std::unordered_map<uint32_t, std::shared_ptr<WebGPUBindGroupLayoutInfo>> m_bindGroupLayouts;
};

} // namespace engine::rendering::webgpu
