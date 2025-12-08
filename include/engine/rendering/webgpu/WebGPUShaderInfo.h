#pragma once

#include <memory>
#include <string>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <spdlog/spdlog.h>

#include "engine/rendering/webgpu/WebGPUBindGroup.h"

namespace engine::rendering::webgpu
{

class WebGPUBindGroupLayoutInfo;
class WebGPUContext;

/**
 * @brief Complete shader metadata with manual reflection information.
 * 
 * Since WebGPU provides no reflection API, WebGPUShaderInfo acts as a manual
 * reflection system that describes:
 * - Shader module and entry points
 * - Bind groups with their buffers (created during shader factory build)
 * - Created GPU resources (shader module, bind group layouts, buffers)
 * 
 * This allows the renderer to:
 * - Create pipelines with correct bind group layouts
 * - Access pre-created buffers for bind groups
 * - Distinguish between global (engine-managed) and per-material buffers
 */
class WebGPUShaderInfo
{
  public:
	/**
	 * @brief Constructs an empty shader info.
	 * Use ShaderFactory to build properly configured instances.
	 */
	WebGPUShaderInfo() = default;

	/**
	 * @brief Constructs a shader info with basic module and entry points.
	 * @param name Shader name for debugging.
	 * @param mod Shader module.
	 * @param vertexEntry Vertex shader entry point.
	 * @param fragmentEntry Fragment shader entry point.
	 */
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
	 * @brief Gets all bind groups with their buffers.
	 * @return Vector of shared pointers to WebGPUBindGroup instances.
	 */
	const std::vector<std::shared_ptr<WebGPUBindGroup>> &getBindGroups() const { return m_bindGroups; }

	/**
	 * @brief Gets a specific bind group by index.
	 * @param groupIndex The bind group index (0, 1, 2, ...).
	 * @return Shared pointer to the bind group, or nullptr if not found.
	 */
	std::shared_ptr<WebGPUBindGroup> getBindGroup(uint32_t groupIndex) const
	{
		if (groupIndex < m_bindGroups.size())
		{
			return m_bindGroups[groupIndex];
		}
		return nullptr;
	}

	/**
	 * @brief Gets all bind group layout infos for pipeline creation.
	 * Extracts layout infos from the bind groups.
	 * @return Vector of bind group layout infos in order of group index.
	 */
	std::vector<std::shared_ptr<WebGPUBindGroupLayoutInfo>> getBindGroupLayoutInfos() const
	{
		std::vector<std::shared_ptr<WebGPUBindGroupLayoutInfo>> layoutInfos;
		layoutInfos.reserve(m_bindGroups.size());
		for (const auto &bg : m_bindGroups)
		{
			if (bg && bg->getLayoutInfo())
			{
				layoutInfos.push_back(bg->getLayoutInfo());
			}
		}
		return layoutInfos;
	}

	/**
	 * @brief Checks if the shader is valid (has module and entry points).
	 */
	bool isValid() const
	{
		return m_module != nullptr && !m_vertexEntryPoint.empty() && !m_fragmentEntryPoint.empty();
	}

	/**
	 * @brief Sets the queue to use for buffer updates.
	 * @param queue The WebGPU queue.
	 */
	void setQueue(wgpu::Queue queue) { m_queue = queue; }

	/**
	 * @brief Starts the render pass by setting all bind groups automatically.
	 * This acts like "startRenderPass" - it configures the render pass with all shader bind groups.
	 * Does not store the render pass - use setBindGroup() to override individual groups if needed.
	 * @param renderPass The render pass encoder.
	 */
	void startRenderPass(wgpu::RenderPassEncoder& renderPass) const
	{
		// Automatically set all bind groups for this shader
		uint32_t setCount = 0;
		for (uint32_t i = 0; i < m_bindGroups.size(); ++i)
		{
			auto bindGroup = m_bindGroups[i];
			if (bindGroup && bindGroup->isValid())
			{
				renderPass.setBindGroup(i, bindGroup->getBindGroup(), 0, nullptr);
				++setCount;
			}
			else
			{
				spdlog::warn("WebGPUShaderInfo '{}': Bind group {} is invalid or not found", m_name, i);
			}
		}
		spdlog::debug("WebGPUShaderInfo '{}': Set {} of {} bind groups for render pass", m_name, setCount, m_bindGroups.size());
	}

	/**
	 * @brief Updates a buffer in a specific bind group (struct template version).
	 * @tparam T The struct type to write.
	 * @param groupIndex The bind group index (0, 1, 2, ...).
	 * @param binding The binding number within the group.
	 * @param data Reference to the struct to write.
	 * @param offset Offset in bytes within the buffer (default: 0).
	 * @return True if update was successful, false if bind group/buffer not found.
	 */
	template<typename T>
	bool updateBindGroupBuffer(uint32_t groupIndex, uint32_t binding, const T& data, size_t offset = 0) const
	{
		if (!m_queue)
		{
			spdlog::error("WebGPUShaderInfo '{}': Queue not set, call setQueue() before updating buffers", m_name);
			return false;
		}
		
		if (groupIndex >= m_bindGroups.size())
		{
			spdlog::warn("WebGPUShaderInfo '{}': Bind group {} out of range (have {} groups)", m_name, groupIndex, m_bindGroups.size());
			return false;
		}
		
		auto bindGroup = m_bindGroups[groupIndex];
		if (!bindGroup || !bindGroup->isValid())
		{
			spdlog::warn("WebGPUShaderInfo '{}': Bind group {} is invalid or not found", m_name, groupIndex);
			return false;
		}
		
		if (!bindGroup->updateBuffer(binding, &data, sizeof(T), offset, m_queue))
		{
			spdlog::warn("WebGPUShaderInfo '{}': Buffer at binding {} in group {} not found or invalid", m_name, binding, groupIndex);
			return false;
		}
		
		return true;
	}

	/**
	 * @brief Updates a buffer in a specific bind group (raw pointer version).
	 * @param groupIndex The bind group index (0, 1, 2, ...).
	 * @param binding The binding number within the group.
	 * @param data Pointer to the data to write.
	 * @param size Size of the data in bytes.
	 * @param offset Offset in bytes within the buffer (default: 0).
	 * @return True if update was successful, false if bind group/buffer not found.
	 */
	bool updateBindGroupBuffer(uint32_t groupIndex, uint32_t binding, const void *data, size_t size, size_t offset = 0) const
	{
		if (!m_queue)
		{
			spdlog::error("WebGPUShaderInfo '{}': Queue not set, call setQueue() before updating buffers", m_name);
			return false;
		}
		
		if (groupIndex >= m_bindGroups.size())
		{
			spdlog::warn("WebGPUShaderInfo '{}': Bind group {} out of range (have {} groups)", m_name, groupIndex, m_bindGroups.size());
			return false;
		}
		
		auto bindGroup = m_bindGroups[groupIndex];
		if (!bindGroup || !bindGroup->isValid())
		{
			spdlog::warn("WebGPUShaderInfo '{}': Bind group {} is invalid or not found", m_name, groupIndex);
			return false;
		}
		
		if (!bindGroup->updateBuffer(binding, data, size, offset, m_queue))
		{
			spdlog::warn("WebGPUShaderInfo '{}': Buffer at binding {} in group {} not found or invalid", m_name, binding, groupIndex);
			return false;
		}
		
		return true;
	}

	/**
	 * @brief Sets the bind group for rendering at a specific group index (explicit render pass version).
	 * @param groupIndex The bind group index (0, 1, 2, ...).
	 * @param renderPass The render pass encoder.
	 * @return True if bind group was set, false if not found.
	 */
	bool setBindGroup(uint32_t groupIndex, wgpu::RenderPassEncoder &renderPass) const
	{
		if (groupIndex >= m_bindGroups.size())
		{
			spdlog::warn("WebGPUShaderInfo '{}': Bind group {} out of range (have {} groups)", m_name, groupIndex, m_bindGroups.size());
			return false;
		}
		
		auto bindGroup = m_bindGroups[groupIndex];
		if (!bindGroup || !bindGroup->isValid())
		{
			spdlog::warn("WebGPUShaderInfo '{}': Bind group {} is invalid or not found", m_name, groupIndex);
			return false;
		}
		
		renderPass.setBindGroup(groupIndex, bindGroup->getBindGroup(), 0, nullptr);
		return true;
	}

	// === Setters (used by ShaderFactory) ===

	void setName(std::string name) { m_name = std::move(name); }
	void setModule(wgpu::ShaderModule mod) { m_module = mod; }
	void setVertexEntryPoint(std::string entry) { m_vertexEntryPoint = std::move(entry); }
	void setFragmentEntryPoint(std::string entry) { m_fragmentEntryPoint = std::move(entry); }
	void addBindGroup(std::shared_ptr<WebGPUBindGroup> bindGroup) { m_bindGroups.push_back(std::move(bindGroup)); }

  private:
	std::string m_name;
	wgpu::ShaderModule m_module = nullptr;
	std::string m_vertexEntryPoint;
	std::string m_fragmentEntryPoint;
	std::vector<std::shared_ptr<WebGPUBindGroup>> m_bindGroups;
	
	// Cached queue and render pass for convenience
	mutable wgpu::Queue m_queue = nullptr;
};

} // namespace engine::rendering::webgpu
