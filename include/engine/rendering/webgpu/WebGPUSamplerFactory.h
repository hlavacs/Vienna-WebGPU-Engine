#pragma once

#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUSamplerFactory
{
  public:
	explicit WebGPUSamplerFactory(WebGPUContext &context);

	wgpu::Sampler createDefaultSampler();

  private:
	WebGPUContext &m_context;
};
} // namespace engine::rendering::webgpu
