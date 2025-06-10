#pragma once
#include <cstddef>
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu {

class WebGPUBufferFactory {
public:
    explicit WebGPUBufferFactory(WebGPUContext& context);

    wgpu::Buffer createUniformBuffer(std::size_t size);
    template<typename T>
    wgpu::Buffer createUniformBuffer(const T* data, std::size_t count);
    template<typename T>
    wgpu::Buffer createUniformBuffer(const std::vector<T>& data);

    wgpu::Buffer createStorageBuffer(std::size_t size);
    template<typename T>
    wgpu::Buffer createStorageBuffer(const T* data, std::size_t count);
    template<typename T>
    wgpu::Buffer createStorageBuffer(const std::vector<T>& data);

    template<typename T>
    wgpu::Buffer createVertexBuffer(const T* data, std::size_t count);
    template<typename T>
    wgpu::Buffer createVertexBuffer(const std::vector<T>& data);

    template<typename T>
    wgpu::Buffer createIndexBuffer(const T* data, std::size_t count);
    template<typename T>
    wgpu::Buffer createIndexBuffer(const std::vector<T>& data);

private:
    WebGPUContext& m_context;
};

} // namespace engine::rendering::webgpu
