#include "engine/rendering/webgpu/WebGPUBufferFactory.h"
#include "engine/rendering/Material.h" // For Material::MaterialProperties

#include <cassert>
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

std::shared_ptr<WebGPUBuffer> WebGPUBufferFactory::createUniformBuffer(
	const std::string &name,
	uint32_t binding,
	std::size_t size
)
{
	wgpu::BufferDescriptor desc = {};
	desc.size = size;
	desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	desc.label = name.c_str();

	wgpu::Buffer buffer = m_context.getDevice().createBuffer(desc);
	return std::make_shared<WebGPUBuffer>(buffer, name, binding, size, static_cast<WGPUBufferUsageFlags>(desc.usage));
}

std::shared_ptr<WebGPUBuffer> WebGPUBufferFactory::createStorageBuffer(
	const std::string &name,
	uint32_t binding,
	std::size_t size
)
{
	wgpu::BufferDescriptor desc = {};
	desc.size = size;
	desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
	desc.label = name.c_str();

	wgpu::Buffer buffer = m_context.getDevice().createBuffer(desc);
	return std::make_shared<WebGPUBuffer>(buffer, name, binding, size, static_cast<WGPUBufferUsageFlags>(desc.usage));
}

std::shared_ptr<WebGPUBuffer> WebGPUBufferFactory::createBufferFromLayoutEntry(
	const WebGPUBindGroupLayoutInfo &layoutInfo,
	uint32_t binding,
	const std::string &name,
	size_t size
)
{
	const wgpu::BindGroupLayoutEntry *entry = layoutInfo.getLayoutEntry(binding);
	assert(entry != nullptr && "Binding not found in layout");

	// Determine buffer size
	size_t bufferSize = size > 0 ? size : static_cast<size_t>(entry->buffer.minBindingSize);
	assert(bufferSize > 0 && "Buffer size must be greater than 0");

	// Create buffer with appropriate usage flags based on buffer type
	wgpu::BufferDescriptor desc;
	desc.size = bufferSize;
	desc.label = name.c_str();

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

	wgpu::Buffer buffer = m_context.getDevice().createBuffer(desc);
	return std::make_shared<WebGPUBuffer>(buffer, name, binding, bufferSize, static_cast<WGPUBufferUsageFlags>(desc.usage));
}

void WebGPUBufferFactory::writeToBuffer(const std::shared_ptr<WebGPUBuffer> &buffer, const void *data, size_t size)
{
	if (!buffer || !data || size == 0) return;
	m_context.getQueue().writeBuffer(buffer->getBuffer(), 0, data, size);
}

} // namespace engine::rendering::webgpu
