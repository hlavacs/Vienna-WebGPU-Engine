#include "engine/rendering/webgpu/WebGPUSwapChainFactory.h"
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

	WebGPUSwapChainFactory::WebGPUSwapChainFactory(WebGPUContext &context)
		: m_context(context) {}
#ifdef __EMSCRIPTEN__
	wgpu::SwapChain WebGPUSwapChainFactory::createSwapChain(wgpu::Surface surface, const wgpu::SwapChainDescriptor &desc)
	{
		return m_context.getDevice().createSwapChain(surface, &desc);
	}
#endif
} // namespace engine::rendering::webgpu
