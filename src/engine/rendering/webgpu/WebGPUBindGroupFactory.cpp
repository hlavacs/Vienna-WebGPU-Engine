#include "engine/rendering/webgpu/WebGPUBindGroupFactory.h"
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu {

WebGPUBindGroupFactory::WebGPUBindGroupFactory(WebGPUContext& context)
    : m_context(context) {}

wgpu::BindGroup WebGPUBindGroupFactory::createMaterialBindGroup(
    wgpu::TextureView texture,
    wgpu::Sampler sampler,
    wgpu::Buffer ubo
) {
    // TODO: Set up bind group layout and entries as needed for your app
    wgpu::BindGroupDescriptor desc = {};
    // ... set up desc ...
    return m_context.getDevice().createBindGroup(desc);
}

} // namespace engine::rendering::webgpu
