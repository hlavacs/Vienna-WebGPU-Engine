#pragma once

#include "engine/rendering/webgpu/WebGPUContext.h"
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu {
class WebGPUSamplerFactory {
public:
    explicit WebGPUSamplerFactory(WebGPUContext& context);

    wgpu::Sampler createDefaultSampler();

private:
    WebGPUContext& m_context;
};
} // namespace engine::rendering::webgpu
