#pragma once

#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUSwapChainFactory
{
  public:
	explicit WebGPUSwapChainFactory(WebGPUContext &context);
#ifdef __EMSCRIPTEN__
	wgpu::SwapChain createSwapChain(wgpu::Surface surface, const wgpu::SwapChainDescriptor &desc);
#endif

  private:
	WebGPUContext &m_context;
};
} // namespace engine::rendering::webgpu
