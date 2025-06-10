#pragma once
#include "engine/rendering/webgpu/WebGPUContext.h"
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu {

class WebGPUBindGroupFactory {
public:
    explicit WebGPUBindGroupFactory(WebGPUContext& context);

    // Example: create a bind group for a material (texture, sampler, uniform buffer)
    wgpu::BindGroup createMaterialBindGroup(wgpu::TextureView texture, wgpu::Sampler sampler, wgpu::Buffer ubo);

    // Optionally: expose default BindGroupLayout
    wgpu::BindGroupLayout getDefaultMaterialBindGroupLayout() const;

private:
    WebGPUContext& m_context;
    wgpu::BindGroupLayout m_defaultMaterialLayout = nullptr;
};

} // namespace engine::rendering::webgpu
