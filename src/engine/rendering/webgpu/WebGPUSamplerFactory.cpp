#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"

#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{
using namespace wgpu;
WebGPUSamplerFactory::WebGPUSamplerFactory(WebGPUContext &context) :
	m_context(context) {}

wgpu::Sampler WebGPUSamplerFactory::createDefaultSampler()
{
	wgpu::SamplerDescriptor samplerDesc;
	// Set up default sampler parameters (e.g., filtering, addressing)
	samplerDesc.addressModeU = AddressMode::Repeat;
	samplerDesc.addressModeV = AddressMode::Repeat;
	samplerDesc.addressModeW = AddressMode::Repeat;
	samplerDesc.magFilter = FilterMode::Linear;
	samplerDesc.minFilter = FilterMode::Linear;
	samplerDesc.mipmapFilter = MipmapFilterMode::Linear;
	samplerDesc.lodMinClamp = 0.0f;
	samplerDesc.lodMaxClamp = 8.0f;
	samplerDesc.compare = CompareFunction::Undefined;
	samplerDesc.maxAnisotropy = 1;
	return m_context.getDevice().createSampler(samplerDesc);
}

} // namespace engine::rendering::webgpu
