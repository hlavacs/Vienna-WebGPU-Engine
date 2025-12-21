#pragma once

#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"

namespace engine::rendering::webgpu
{

/**
 * @class WebGPUBindGroup
 * @brief GPU-side bind group: wraps a WebGPU bind group with its layout and associated buffers.
 *
 * This class encapsulates a WebGPU bind group along with a reference to its layout info
 * and the WebGPUBuffer instances it uses. Provides accessors for all relevant properties and ensures
 * resource cleanup. Used for managing bind groups throughout the rendering pipeline.
 */
class WebGPUBindGroup
{
  public:
	/**
	 * @brief Constructs a WebGPUBindGroup from bind group, layout, and buffers.
	 *
	 * @param bindGroup The GPU-side bind group (can be null for layout-only groups).
	 * @param layoutInfo Shared pointer to the bind group layout info.
	 * @param buffers The WebGPUBuffer instances used by this bind group.
	 *
	 * @throws Assertion failure if layout info is invalid.
	 */
	WebGPUBindGroup(
		wgpu::BindGroup bindGroup,
		std::shared_ptr<WebGPUBindGroupLayoutInfo> layoutInfo,
		std::vector<std::shared_ptr<WebGPUBuffer>> buffers
	) : m_bindGroup(bindGroup),
		m_layoutInfo(layoutInfo),
		m_buffers(std::move(buffers))
	{
		// Note: bindGroup can be null for layout-only groups (e.g., texture groups managed by material system)
		assert(m_layoutInfo && "BindGroupLayoutInfo must be valid");
	}

	/**
	 * @brief Default constructor.
	 */
	WebGPUBindGroup() = default;

	/**
	 * @brief Destructor that cleans up WebGPU resources.
	 *
	 * Releases the bind group. Buffers are managed via shared_ptr.
	 */
	~WebGPUBindGroup()
	{
		if (m_bindGroup)
		{
			m_bindGroup.release();
		}
	}

	// Delete copy, enable move
	WebGPUBindGroup(const WebGPUBindGroup &) = delete;
	WebGPUBindGroup &operator=(const WebGPUBindGroup &) = delete;
	WebGPUBindGroup(WebGPUBindGroup &&) noexcept = default;
	WebGPUBindGroup &operator=(WebGPUBindGroup &&) noexcept = default;

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
	 * @return Vector of shared pointers to WebGPUBuffer instances.
	 */
	const std::vector<std::shared_ptr<WebGPUBuffer>> &getBuffers() const { return m_buffers; }

	/**
	 * @brief Gets a specific buffer by index.
	 * @param index Index of the buffer to retrieve.
	 * @return Shared pointer to the buffer at the specified index.
	 * @throws Assertion failure if index is out of bounds.
	 */
	std::shared_ptr<WebGPUBuffer> getBuffer(size_t index) const
	{
		assert(index < m_buffers.size() && "Buffer index out of bounds");
		return m_buffers[index];
	}

	/**
	 * @brief Finds a buffer by binding number.
	 * @param binding The binding number to search for.
	 * @return Shared pointer to the buffer if found, nullptr otherwise.
	 */
	std::shared_ptr<WebGPUBuffer> findBufferByBinding(uint32_t binding) const
	{
		for (const auto &buffer : m_buffers)
		{
			if (buffer && buffer->getBinding() == binding)
			{
				return buffer;
			}
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
	 * @return True if the buffer was found and updated successfully, false otherwise.
	 */
	bool updateBuffer(uint32_t binding, const void *data, size_t size, size_t offset, wgpu::Queue queue) const
	{
		auto buffer = findBufferByBinding(binding);
		if (!buffer || !buffer->isValid())
		{
			return false;
		}
		
		queue.writeBuffer(buffer->getBuffer(), offset, data, size);
		return true;
	}

	/**
	 * @brief Checks if this bind group is valid.
	 * @return True if bind group and layout are valid.
	 */
	bool isValid() const
	{
		return m_bindGroup != nullptr && m_layoutInfo != nullptr;
	}

	/**
	 * @brief Adds a buffer to this bind group's buffer list.
	 * @param buffer The buffer to add.
	 */
	void addBuffer(std::shared_ptr<WebGPUBuffer> buffer)
	{
		m_buffers.push_back(std::move(buffer));
	}

  private:
	/**
	 * @brief The underlying WebGPU bind group resource.
	 */
	wgpu::BindGroup m_bindGroup;

	/**
	 * @brief Reference to the bind group layout info.
	 */
	std::shared_ptr<WebGPUBindGroupLayoutInfo> m_layoutInfo;

	/**
	 * @brief WebGPUBuffer instances used by this bind group.
	 */
	std::vector<std::shared_ptr<WebGPUBuffer>> m_buffers;
};

} // namespace engine::rendering::webgpu