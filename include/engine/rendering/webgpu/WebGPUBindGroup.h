#pragma once

#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"

namespace engine::rendering::webgpu
{

/**
 * @class WebGPUBindGroup
 * @brief GPU-side bind group: wraps a WebGPU bind group with its layout and associated buffers.
 *
 * This class encapsulates a WebGPU bind group along with a reference to its layout info
 * and the buffers it uses. It provides accessors for all relevant properties and ensures
 * resource cleanup. Used for managing bind groups throughout the rendering pipeline.
 */
class WebGPUBindGroup
{
  public:
	/**
	 * @brief Constructs a WebGPUBindGroupInfo from bind group, layout, and buffers.
	 *
	 * @param bindGroup The GPU-side bind group.
	 * @param layoutInfo Shared pointer to the bind group layout info.
	 * @param buffers The buffers used by this bind group.
	 *
	 * @throws Assertion failure if bind group or layout info is invalid.
	 */
	WebGPUBindGroup(
		wgpu::BindGroup bindGroup,
		std::shared_ptr<WebGPUBindGroupLayoutInfo> layoutInfo,
		const std::vector<wgpu::Buffer> &buffers
	) : m_bindGroup(bindGroup),
		m_layoutInfo(layoutInfo),
		m_buffers(buffers)
	{
		assert(m_bindGroup && "BindGroup must be valid");
		assert(m_layoutInfo && "BindGroupLayoutInfo must be valid");
		assert(m_buffers.size() <= m_layoutInfo->getEntryCount() && "Too many buffers for layout");
	}

	/**
	 * @brief Destructor that cleans up WebGPU resources.
	 *
	 * Releases the bind group and associated buffers to prevent memory leaks.
	 */
	~WebGPUBindGroup()
	{
		if (m_bindGroup)
		{
			m_bindGroup.release();
		}

		// Release all buffers
		for (auto &buffer : m_buffers)
		{
			if (buffer)
			{
				buffer.destroy();
				buffer.release();
			}
		}
	}

	/**
	 * @brief Gets the underlying WebGPU bind group.
	 * @return The WebGPU bind group object.
	 */
	wgpu::BindGroup getBindGroup() const { return m_bindGroup; }

	/**
	 * @brief Gets the bind group layout info.
	 * @return Shared pointer to the bind group layout info.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> getLayoutInfo() const { return m_layoutInfo; }

	/**
	 * @brief Gets the buffers used by this bind group.
	 * @return Vector of buffers.
	 */
	const std::vector<wgpu::Buffer> &getBuffers() const { return m_buffers; }

	/**
	 * @brief Gets a specific buffer by index.
	 * @param index Index of the buffer to retrieve.
	 * @return The buffer at the specified index.
	 * @throws Assertion failure if index is out of bounds.
	 */
	wgpu::Buffer getBuffer(size_t index) const
	{
		assert(index < m_buffers.size() && "Buffer index out of bounds");
		return m_buffers[index];
	}

	/**
	 * @brief Finds a buffer by binding number.
	 * @param binding The binding number to search for.
	 * @return The buffer if found, nullptr otherwise.
	 */
	wgpu::Buffer findBufferByBinding(uint32_t binding) const
	{
		if (binding < m_buffers.size())
		{
			return m_buffers[binding];
		}
		return nullptr;
	}

	/**
	 * @brief Gets the number of buffers in this bind group.
	 * @return Number of buffers.
	 */
	size_t getBufferCount() const { return m_buffers.size(); }

	/**
	 * @brief Updates buffer data at a specific binding.
	 * @param binding The binding number of the buffer to update.
	 * @param data Pointer to the data to write.
	 * @param size Size of the data in bytes.
	 * @param offset Offset within the buffer to start writing.
	 * @param queue The WebGPU queue to use for the write operation.
	 */
	void updateBuffer(uint32_t binding, const void *data, size_t size, size_t offset, wgpu::Queue queue) const
	{
		wgpu::Buffer buffer = findBufferByBinding(binding);
		assert(buffer && "Buffer not found for binding");
		queue.writeBuffer(buffer, offset, data, size);
	}

  protected:
	/**
	 * @brief The underlying WebGPU bind group resource.
	 */
	wgpu::BindGroup m_bindGroup;

	/**
	 * @brief Reference to the bind group layout info.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> m_layoutInfo;

	/**
	 * @brief Buffers used by this bind group.
	 */
	std::vector<wgpu::Buffer> m_buffers;
};

} // namespace engine::rendering::webgpu