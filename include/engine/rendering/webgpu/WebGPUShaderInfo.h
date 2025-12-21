#pragma once

#include <memory>
#include <string>
#include <vector>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

class WebGPUBindGroupLayoutInfo;

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
  public:
	// Constructors
	WebGPUShaderInfo() = default;

	WebGPUShaderInfo(
		std::string name,
		wgpu::ShaderModule mod,
		std::string vertexEntry,
		std::string fragmentEntry
	) : m_name(std::move(name)),
		m_module(mod),
		m_vertexEntryPoint(std::move(vertexEntry)),
		m_fragmentEntryPoint(std::move(fragmentEntry))
	{
	}

	// === Getters ===
	const std::string &getName() const { return m_name; }
	wgpu::ShaderModule getModule() const { return m_module; }
	const std::string &getVertexEntryPoint() const { return m_vertexEntryPoint; }
	const std::string &getFragmentEntryPoint() const { return m_fragmentEntryPoint; }

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

	/**
	 * @brief Set the shader name.
	 * @param name The shader name.
	 */
	void setName(std::string name) { m_name = std::move(name); }

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
	 * @brief Add a bind group layout at the specified index.
	 * @param groupIndex The bind group index.
	 */
	void addBindGroupLayout(uint32_t groupIndex, std::shared_ptr<WebGPUBindGroupLayoutInfo> layout) { m_bindGroupLayouts[groupIndex] = std::move(layout); }

  private:
	std::string m_name;
	wgpu::ShaderModule m_module = nullptr;
	std::string m_vertexEntryPoint;
	std::string m_fragmentEntryPoint;

	std::unordered_map<uint32_t, std::shared_ptr<WebGPUBindGroupLayoutInfo>> m_bindGroupLayouts;
};

} // namespace engine::rendering::webgpu
