#pragma once

#include <cstddef>
#include <string>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

/**
 * @brief Wrapper for WebGPU buffer with metadata.
 *
 * Encapsulates a GPU buffer along with its binding information, usage flags,
 * and whether it's a global (engine-managed) or per-material buffer.
 */
class WebGPUBuffer
{
  public:
	/**
	 * @brief Constructs a WebGPUBuffer with metadata.
	 * @param buffer The GPU buffer.
	 * @param name Debug name for the buffer.
	 * @param binding Binding index within its bind group.
	 * @param size Size of the buffer in bytes.
	 * @param usage Buffer usage flags (raw flags).
	 * @param queue Queue used by write() to upload data into the buffer.
	 */
	WebGPUBuffer(
		wgpu::Buffer buffer,
		std::string name,
		uint32_t binding,
		size_t size,
		WGPUBufferUsageFlags usage,
		wgpu::Queue queue
	) : m_buffer(buffer),
		m_name(std::move(name)),
		m_binding(binding),
		m_size(size),
		m_usage(usage),
		m_queue(queue)
	{
	}

	/**
	 * @brief Default constructor for empty buffer.
	 */
	WebGPUBuffer() = default;

	/**
	 * @brief Destructor that releases GPU resources.
	 */
	~WebGPUBuffer()
	{
		if (m_buffer)
		{
			m_buffer.destroy();
			m_buffer.release();
		}
	}

	// Delete copy constructor and assignment
	WebGPUBuffer(const WebGPUBuffer &) = delete;
	WebGPUBuffer &operator=(const WebGPUBuffer &) = delete;

	// Enable move constructor and assignment
	WebGPUBuffer(WebGPUBuffer &&other) noexcept
		: m_buffer(other.m_buffer),
		  m_name(std::move(other.m_name)),
		  m_binding(other.m_binding),
		  m_size(other.m_size),
		  m_usage(other.m_usage),
		  m_queue(other.m_queue)
	{
		other.m_buffer = nullptr;
	}

	WebGPUBuffer &operator=(WebGPUBuffer &&other) noexcept
	{
		if (this != &other)
		{
			// Release current buffer
			if (m_buffer)
			{
				m_buffer.destroy();
				m_buffer.release();
			}

			// Move from other
			m_buffer = other.m_buffer;
			m_name = std::move(other.m_name);
			m_binding = other.m_binding;
			m_size = other.m_size;
			m_usage = other.m_usage;
			m_queue = other.m_queue;

			other.m_buffer = nullptr;
		}
		return *this;
	}

	// === Getters ===

	[[nodiscard]] wgpu::Buffer getBuffer() const { return m_buffer; }
	[[nodiscard]] const std::string &getName() const { return m_name; }
	[[nodiscard]] uint32_t getBinding() const { return m_binding; }
	[[nodiscard]] size_t getSize() const { return m_size; }
	[[nodiscard]] WGPUBufferUsageFlags getUsage() const { return m_usage; }
	[[nodiscard]] bool isValid() const { return m_buffer != nullptr; }

	// === Writes ===

	/**
	 * @brief Upload @p size bytes from @p data into the buffer at @p offset.
	 *
	 * Uses the queue captured at construction, so call sites no longer reach
	 * for WebGPUContext::getQueue(). No-op if the buffer or queue is null or
	 * there is nothing to write.
	 */
	void write(const void *data, size_t size, size_t offset = 0) const
	{
		if (m_buffer && m_queue && data && size > 0)
		{
			// wgpu::Queue::writeBuffer is non-const in the C++ wrapper; copy the
			// handle (cheap — it's a non-owning pointer) so write() stays const.
			wgpu::Queue queue = m_queue;
			queue.writeBuffer(m_buffer, offset, data, size);
		}
	}

  private:
	wgpu::Buffer m_buffer = nullptr;
	std::string m_name;
	uint32_t m_binding = 0;
	size_t m_size = 0;
	WGPUBufferUsageFlags m_usage = 0;
	wgpu::Queue m_queue = nullptr;
};

} // namespace engine::rendering::webgpu
