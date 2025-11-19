#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/Material.h" // For Material::MaterialProperties

#include <cstring>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{

WebGPUBufferFactory::WebGPUBufferFactory(WebGPUContext &context) :
	m_context(context) {}

wgpu::Buffer WebGPUBufferFactory::createBuffer(const wgpu::BufferDescriptor &desc)
{
	return m_context.getDevice().createBuffer(desc);
}

wgpu::Buffer WebGPUBufferFactory::createBufferWithData(const void *data, size_t size, wgpu::BufferUsage usage, bool mappedAtCreation)
{
	wgpu::BufferDescriptor desc;
	desc.size = size;
	desc.usage = usage | wgpu::BufferUsage::CopyDst;
	desc.mappedAtCreation = mappedAtCreation;

	wgpu::Buffer buffer = m_context.getDevice().createBuffer(desc);

	if (mappedAtCreation && size > 0 && data)
	{
		void *mapped = buffer.getMappedRange(0, size);
		memcpy(mapped, data, size);
		buffer.unmap();
	}
	else if (!mappedAtCreation && size > 0 && data)
	{
		m_context.getQueue().writeBuffer(buffer, 0, data, size);
	}

	return buffer;
}

wgpu::Buffer WebGPUBufferFactory::createUniformBuffer(std::size_t size)
{
	wgpu::BufferDescriptor desc = {};
	desc.size = size;
	desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	return m_context.getDevice().createBuffer(desc);
}

template <typename T>
wgpu::Buffer WebGPUBufferFactory::createUniformBuffer(const T *data, std::size_t count)
{
	wgpu::Buffer buffer = createUniformBuffer(sizeof(T) * count);
	m_context.getQueue().writeBuffer(buffer, 0, data, sizeof(T) * count);
	return buffer;
}

template <typename T>
wgpu::Buffer WebGPUBufferFactory::createUniformBuffer(const std::vector<T> &data)
{
	return createUniformBuffer(data.data(), data.size());
}

wgpu::Buffer WebGPUBufferFactory::createStorageBuffer(std::size_t size)
{
	wgpu::BufferDescriptor desc = {};
	desc.size = size;
	desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
	return m_context.getDevice().createBuffer(desc);
}

template <typename T>
wgpu::Buffer WebGPUBufferFactory::createStorageBuffer(const T *data, std::size_t count)
{
	wgpu::Buffer buffer = createStorageBuffer(sizeof(T) * count);
	m_context.getQueue().writeBuffer(buffer, 0, data, sizeof(T) * count);
	return buffer;
}

template <typename T>
wgpu::Buffer WebGPUBufferFactory::createStorageBuffer(const std::vector<T> &data)
{
	return createStorageBuffer(data.data(), data.size());
}

template <typename T>
wgpu::Buffer WebGPUBufferFactory::createVertexBuffer(const T *data, std::size_t count)
{
	wgpu::BufferDescriptor desc = {};
	desc.size = sizeof(T) * count;
	desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
	wgpu::Buffer buffer = m_context.getDevice().createBuffer(desc);
	m_context.getQueue().writeBuffer(buffer, 0, data, sizeof(T) * count);
	return buffer;
}

template <typename T>
wgpu::Buffer WebGPUBufferFactory::createVertexBuffer(const std::vector<T> &data)
{
	return createVertexBuffer(data.data(), data.size());
}

template <typename T>
wgpu::Buffer WebGPUBufferFactory::createIndexBuffer(const T *data, std::size_t count)
{
	wgpu::BufferDescriptor desc = {};
	desc.size = sizeof(T) * count;
	desc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
	wgpu::Buffer buffer = m_context.getDevice().createBuffer(desc);
	m_context.getQueue().writeBuffer(buffer, 0, data, sizeof(T) * count);
	return buffer;
}

template <typename T>
wgpu::Buffer WebGPUBufferFactory::createIndexBuffer(const std::vector<T> &data)
{
	return createIndexBuffer(data.data(), data.size());
}

wgpu::Buffer WebGPUBufferFactory::createBufferFromLayoutEntry(
	const WebGPUBindGroupLayoutInfo &layoutInfo,
	uint32_t binding,
	size_t size
)
{
	const wgpu::BindGroupLayoutEntry *entry = layoutInfo.findEntryByBinding(binding);
	assert(entry != nullptr && "Binding not found in layout");

	// Determine buffer size
	size_t bufferSize = size > 0 ? size : static_cast<size_t>(entry->buffer.minBindingSize);
	assert(bufferSize > 0 && "Buffer size must be greater than 0");

	// Create buffer with appropriate usage flags based on buffer type
	wgpu::BufferDescriptor desc;
	desc.size = bufferSize;
	
	if (entry->buffer.type == wgpu::BufferBindingType::Uniform)
	{
		desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	}
	else if (entry->buffer.type == wgpu::BufferBindingType::Storage || entry->buffer.type == wgpu::BufferBindingType::ReadOnlyStorage)
	{
		desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
	}
	else
	{
		desc.usage = wgpu::BufferUsage::CopyDst;
	}
	
	return m_context.getDevice().createBuffer(desc);
}// Explicit template instantiations for common types
// (Add more as needed for your engine)
template wgpu::Buffer WebGPUBufferFactory::createUniformBuffer<float>(const float *, std::size_t);
template wgpu::Buffer WebGPUBufferFactory::createUniformBuffer<float>(const std::vector<float> &);
template wgpu::Buffer WebGPUBufferFactory::createUniformBuffer<engine::rendering::Material::MaterialProperties>(
	const engine::rendering::Material::MaterialProperties *, std::size_t
);
template wgpu::Buffer WebGPUBufferFactory::createStorageBuffer<float>(const float *, std::size_t);
template wgpu::Buffer WebGPUBufferFactory::createStorageBuffer<float>(const std::vector<float> &);
template wgpu::Buffer WebGPUBufferFactory::createVertexBuffer<float>(const float *, std::size_t);
template wgpu::Buffer WebGPUBufferFactory::createVertexBuffer<float>(const std::vector<float> &);
template wgpu::Buffer WebGPUBufferFactory::createIndexBuffer<uint16_t>(const uint16_t *, std::size_t);
template wgpu::Buffer WebGPUBufferFactory::createIndexBuffer<uint16_t>(const std::vector<uint16_t> &);

} // namespace engine::rendering::webgpu
