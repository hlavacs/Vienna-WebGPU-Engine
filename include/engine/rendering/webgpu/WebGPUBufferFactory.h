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

/**
 * @brief Builder for GPU buffers — raw wgpu::Buffer for low-level needs,
 *        wrapped WebGPUBuffer (default) for everything that binds to a
 *        shader.
 *
 * **Naming convention**
 *   - `createX(name, binding, ...)` returns `shared_ptr<WebGPUBuffer>` —
 *     carries name/binding/size for the bind-group factory and matches the
 *     rest of the engine's wrapper convention. Use this for UBOs / SSBOs.
 *   - `createBuffer(desc)` / `createBufferWithData(...)` return raw
 *     `wgpu::Buffer` — for low-level needs like vertex / index buffers
 *     that the mesh layer manages directly.
 *
 * BufferFactory deliberately has no cache: every buffer is a unique
 * resource owned by its caller (a material, a scene-light buffer, a
 * mesh). There's no descriptor-keyed identity worth deduplicating.
 */
class WebGPUBufferFactory
{
  public:
	explicit WebGPUBufferFactory(WebGPUContext &context);

	// === Raw wgpu::Buffer creation (low-level, no engine wrapper) ===

	/// Generic GPU buffer from an explicit descriptor.
	wgpu::Buffer createBuffer(const wgpu::BufferDescriptor &desc);

	/// Buffer pre-populated with @p data of @p size bytes. The CopyDst usage
	/// bit is OR-ed in so the buffer can be written via queue.writeBuffer
	/// later. If @p mappedAtCreation is true (default), data is mapped+copied
	/// at construction; otherwise it's uploaded via the queue.
	wgpu::Buffer createBufferWithData(const void *data, size_t size, wgpu::BufferUsage usage, bool mappedAtCreation = true);

	/// Vector-flavoured overload. Size is `vec.size() * sizeof(T)`.
	template <typename T>
	wgpu::Buffer createBufferWithData(const std::vector<T> &vec, wgpu::BufferUsage usage, bool mappedAtCreation = true)
	{
		return createBufferWithData(vec.data(), vec.size() * sizeof(T), usage, mappedAtCreation);
	}

	// === WebGPUBuffer creation (engine wrapper — default for UBO/SSBO) ===

	/// Construct a WebGPUBuffer from a bind-group layout entry: usage flags
	/// derive from the entry's buffer type, size from minBindingSize unless
	/// an explicit @p size is supplied. No data uploaded — caller writes
	/// via queue.writeBuffer or via WebGPUBuffer's update helpers.
	std::shared_ptr<WebGPUBuffer> createBufferFromLayoutEntry(
		const WebGPUBindGroupLayoutInfo &layoutInfo,
		uint32_t binding,
		const std::string &name,
		size_t size = 0
	);

	/// Empty uniform buffer of @p size bytes, wrapped as WebGPUBuffer with
	/// the supplied debug @p name and shader @p binding.
	std::shared_ptr<WebGPUBuffer> createUniformBuffer(
		const std::string &name,
		uint32_t binding,
		std::size_t size
	);

	/// Uniform buffer pre-populated with @p count items from @p data.
	template <typename T>
	std::shared_ptr<WebGPUBuffer> createUniformBuffer(
		const std::string &name,
		uint32_t binding,
		const T *data,
		std::size_t count
	)
	{
		size_t size = sizeof(T) * count;
		auto buffer = createUniformBuffer(name, binding, size);
		if (data && count > 0)
		{
			writeToBuffer(buffer, data, size);
		}
		return buffer;
	}

	/// Uniform buffer pre-populated from a vector.
	template <typename T>
	std::shared_ptr<WebGPUBuffer> createUniformBuffer(
		const std::string &name,
		uint32_t binding,
		const std::vector<T> &data
	)
	{
		return createUniformBuffer(name, binding, data.data(), data.size());
	}

	/// Empty storage buffer.
	std::shared_ptr<WebGPUBuffer> createStorageBuffer(
		const std::string &name,
		uint32_t binding,
		std::size_t size
	);

	/// Storage buffer pre-populated with @p count items from @p data.
	template <typename T>
	std::shared_ptr<WebGPUBuffer> createStorageBuffer(
		const std::string &name,
		uint32_t binding,
		const T *data,
		std::size_t count
	)
	{
		size_t size = sizeof(T) * count;
		auto buffer = createStorageBuffer(name, binding, size);
		if (data && count > 0)
		{
			writeToBuffer(buffer, data, size);
		}
		return buffer;
	}

	/// Storage buffer pre-populated from a vector.
	template <typename T>
	std::shared_ptr<WebGPUBuffer> createStorageBuffer(
		const std::string &name,
		uint32_t binding,
		const std::vector<T> &data
	)
	{
		return createStorageBuffer(name, binding, data.data(), data.size());
	}

  private:
	WebGPUContext &m_context;

	/// Helper: write @p size bytes from @p data to the wrapped buffer via
	/// queue.writeBuffer. Lives here so the template overloads above don't
	/// have to pull in WebGPUContext's full header transitively.
	void writeToBuffer(const std::shared_ptr<WebGPUBuffer> &buffer, const void *data, size_t size);
};

} // namespace engine::rendering::webgpu
