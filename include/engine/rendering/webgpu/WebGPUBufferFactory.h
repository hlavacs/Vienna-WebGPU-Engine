#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUBuffer.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUBufferFactory
{
  public:
	explicit WebGPUBufferFactory(WebGPUContext &context);

	/**
	 * @brief Creates a generic GPU buffer with the given descriptor.
	 */
	wgpu::Buffer createBuffer(const wgpu::BufferDescriptor &desc);

	/**
	 * @brief Creates and uploads data to a GPU buffer.
	 * @param data Pointer to data to upload.
	 * @param size Size in bytes.
	 * @param usage Buffer usage flags.
	 * @param mappedAtCreation Whether to create the buffer with mappedAtCreation=true for initial data.
	 */
	wgpu::Buffer createBufferWithData(const void *data, size_t size, wgpu::BufferUsage usage, bool mappedAtCreation = true);

	/**
	 * @brief Creates and uploads data from a std::vector<T>.
	 */
	template <typename T>
	wgpu::Buffer createBufferWithData(const std::vector<T> &vec, wgpu::BufferUsage usage, bool mappedAtCreation = true)
	{
		return createBufferWithData(vec.data(), vec.size() * sizeof(T), usage, mappedAtCreation);
	}

	/**
	 * @brief Creates a WebGPUBuffer from a bind group layout entry.
	 *
	 * Creates a buffer with the appropriate usage flags based on the layout entry's buffer type.
	 * The buffer is created with the size from the layout entry's minBindingSize, or a custom size if provided.
	 * No data is uploaded - use queue.writeBuffer() or mappedAtCreation separately to upload data.
	 *
	 * @param layoutInfo The bind group layout info containing the entry.
	 * @param binding The binding number to create buffer for.
	 * @param name Debug name for the buffer.
	 * @param size The size in bytes (if 0, uses minBindingSize from entry).
	 * @return Shared pointer to WebGPUBuffer configured according to the layout entry.
	 */
	std::shared_ptr<WebGPUBuffer> createBufferFromLayoutEntry(
		const WebGPUBindGroupLayoutInfo &layoutInfo,
		uint32_t binding,
		const std::string &name,
		size_t size = 0
	);

	// === WebGPUBuffer Creation Methods ===

	/**
	 * @brief Creates a uniform buffer wrapped in WebGPUBuffer.
	 * @param name Debug name for the buffer.
	 * @param binding Binding index in the shader.
	 * @param size Size in bytes.
	 */
	std::shared_ptr<WebGPUBuffer> createUniformBufferWrapped(
		const std::string &name,
		uint32_t binding,
		std::size_t size
	);

	/**
	 * @brief Creates a uniform buffer with data, wrapped in WebGPUBuffer.
	 */
	template <typename T>
	std::shared_ptr<WebGPUBuffer> createUniformBufferWrapped(
		const std::string &name,
		uint32_t binding,
		const T *data,
		std::size_t count
	);

	/**
	 * @brief Creates a uniform buffer from vector, wrapped in WebGPUBuffer.
	 */
	template <typename T>
	std::shared_ptr<WebGPUBuffer> createUniformBufferWrapped(
		const std::string &name,
		uint32_t binding,
		const std::vector<T> &data
	);

	/**
	 * @brief Creates a storage buffer wrapped in WebGPUBuffer.
	 */
	std::shared_ptr<WebGPUBuffer> createStorageBufferWrapped(
		const std::string &name,
		uint32_t binding,
		std::size_t size
	);

	/**
	 * @brief Creates a storage buffer with data, wrapped in WebGPUBuffer.
	 */
	template <typename T>
	std::shared_ptr<WebGPUBuffer> createStorageBufferWrapped(
		const std::string &name,
		uint32_t binding,
		const T *data,
		std::size_t count
	);

	/**
	 * @brief Creates a storage buffer from vector, wrapped in WebGPUBuffer.
	 */
	template <typename T>
	std::shared_ptr<WebGPUBuffer> createStorageBufferWrapped(
		const std::string &name,
		uint32_t binding,
		const std::vector<T> &data
	);

	// === Legacy Raw Buffer Creation Methods (deprecated, kept for compatibility) ===

	wgpu::Buffer createUniformBuffer(std::size_t size);
	template <typename T>
	wgpu::Buffer createUniformBuffer(const T *data, std::size_t count);
	template <typename T>
	wgpu::Buffer createUniformBuffer(const std::vector<T> &data);

	wgpu::Buffer createStorageBuffer(std::size_t size);
	template <typename T>
	wgpu::Buffer createStorageBuffer(const T *data, std::size_t count);
	template <typename T>
	wgpu::Buffer createStorageBuffer(const std::vector<T> &data);

	template <typename T>
	wgpu::Buffer createVertexBuffer(const T *data, std::size_t count);
	template <typename T>
	wgpu::Buffer createVertexBuffer(const std::vector<T> &data);

	template <typename T>
	wgpu::Buffer createIndexBuffer(const T *data, std::size_t count);
	template <typename T>
	wgpu::Buffer createIndexBuffer(const std::vector<T> &data);

  private:
	WebGPUContext &m_context;
};

} // namespace engine::rendering::webgpu
