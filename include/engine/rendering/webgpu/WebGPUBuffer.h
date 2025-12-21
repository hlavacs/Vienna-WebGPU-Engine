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
	 */
	WebGPUBuffer(
		wgpu::Buffer buffer,
		std::string name,
		uint32_t binding,
		size_t size,
		WGPUBufferUsageFlags usage
	) : m_buffer(buffer),
		m_name(std::move(name)),
		m_binding(binding),
		m_size(size),
		m_usage(usage)
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
		  m_usage(other.m_usage)
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

			other.m_buffer = nullptr;
		}
		return *this;
	}

	// === Getters ===

	wgpu::Buffer getBuffer() const { return m_buffer; }
	const std::string &getName() const { return m_name; }
	uint32_t getBinding() const { return m_binding; }
	size_t getSize() const { return m_size; }
	WGPUBufferUsageFlags getUsage() const { return m_usage; }
	bool isValid() const { return m_buffer != nullptr; }

  private:
	wgpu::Buffer m_buffer = nullptr;
	std::string m_name;
	uint32_t m_binding = 0;
	size_t m_size = 0;
	WGPUBufferUsageFlags m_usage = 0;
};

} // namespace engine::rendering::webgpu
