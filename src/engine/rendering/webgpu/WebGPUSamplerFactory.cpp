#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"

#include <spdlog/spdlog.h>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{
using namespace wgpu;

WebGPUSamplerFactory::WebGPUSamplerFactory(WebGPUContext &context) :
	m_context(context) {}

wgpu::Sampler WebGPUSamplerFactory::getSampler(const std::string &name)
{
	// already cached?
	auto it = m_samplerCache.find(name);
	if (it != m_samplerCache.end())
		return it->second;

	// lazy-create only known defaults
	if (name == SamplerNames::DEFAULT)
		return createDefaultSampler();

	if (name == SamplerNames::MIPMAP_LINEAR)
		return createMipmapSampler();

	if (name == SamplerNames::CLAMP_LINEAR)
		return createClampLinearSampler();

	if (name == SamplerNames::REPEAT_LINEAR)
		return createRepeatLinearSampler();

	return getSampler(SamplerNames::DEFAULT);
}

wgpu::Sampler WebGPUSamplerFactory::createSampler(
	const std::string &name,
	const wgpu::SamplerDescriptor &desc
)
{
	wgpu::Sampler sampler = m_context.getDevice().createSampler(desc);
	registerSampler(name, sampler);
	return sampler;
}

void WebGPUSamplerFactory::registerSampler(const std::string &name, wgpu::Sampler sampler)
{
	if (!sampler)
	{
		spdlog::warn("Attempted to register null sampler with name '{}'", name);
		return;
	}

	auto it = m_samplerCache.find(name);
	if (it != m_samplerCache.end())
	{
		spdlog::warn("Sampler '{}' already exists, replacing it", name);
		it->second.release();
		it->second = sampler;
	}
	else
	{
		m_samplerCache.insert({name, sampler});
	}
}

wgpu::Sampler WebGPUSamplerFactory::getDefaultSampler()
{
	return getSampler(SamplerNames::DEFAULT);
}

wgpu::Sampler WebGPUSamplerFactory::getMipmapSampler()
{
	return getSampler(SamplerNames::MIPMAP_LINEAR);
}

wgpu::Sampler WebGPUSamplerFactory::getClampLinearSampler()
{
	return getSampler(SamplerNames::CLAMP_LINEAR);
}

wgpu::Sampler WebGPUSamplerFactory::getRepeatLinearSampler()
{
	return getSampler(SamplerNames::REPEAT_LINEAR);
}

void WebGPUSamplerFactory::cleanup()
{
	for (auto &[name, sampler] : m_samplerCache)
	{
		sampler.release();
	}
	m_samplerCache.clear();
}

wgpu::Sampler WebGPUSamplerFactory::createDefaultSampler()
{
	wgpu::SamplerDescriptor desc{};
	desc.addressModeU = AddressMode::Repeat;
	desc.addressModeV = AddressMode::Repeat;
	desc.addressModeW = AddressMode::Repeat;
	desc.magFilter = FilterMode::Linear;
	desc.minFilter = FilterMode::Linear;
	desc.mipmapFilter = MipmapFilterMode::Linear;
	desc.lodMinClamp = 0.0f;
	desc.lodMaxClamp = 8.0f;
	desc.compare = CompareFunction::Undefined;
	desc.maxAnisotropy = 1;

	return createSampler(SamplerNames::DEFAULT, desc);
}

wgpu::Sampler WebGPUSamplerFactory::createMipmapSampler()
{
	wgpu::SamplerDescriptor samplerDesc{};
	// Mipmap generation sampler (clamp to edge, linear filtering)
	samplerDesc.addressModeU = AddressMode::ClampToEdge;
	samplerDesc.addressModeV = AddressMode::ClampToEdge;
	samplerDesc.addressModeW = AddressMode::ClampToEdge;
	samplerDesc.magFilter = FilterMode::Linear;
	samplerDesc.minFilter = FilterMode::Linear;
	samplerDesc.mipmapFilter = MipmapFilterMode::Linear;
	samplerDesc.lodMinClamp = 0.0f;
	samplerDesc.lodMaxClamp = 1.0f;
	samplerDesc.compare = CompareFunction::Undefined;
	samplerDesc.maxAnisotropy = 1;

	return createSampler(SamplerNames::MIPMAP_LINEAR, samplerDesc);
}

wgpu::Sampler WebGPUSamplerFactory::createClampLinearSampler()
{
	wgpu::SamplerDescriptor samplerDesc{};
	// Clamp sampler (clamp to edge, linear filtering)
	samplerDesc.addressModeU = AddressMode::ClampToEdge;
	samplerDesc.addressModeV = AddressMode::ClampToEdge;
	samplerDesc.addressModeW = AddressMode::ClampToEdge;
	samplerDesc.magFilter = FilterMode::Linear;
	samplerDesc.minFilter = FilterMode::Linear;
	samplerDesc.mipmapFilter = MipmapFilterMode::Linear;
	samplerDesc.lodMinClamp = 0.0f;
	samplerDesc.lodMaxClamp = 8.0f;
	samplerDesc.compare = CompareFunction::Undefined;
	samplerDesc.maxAnisotropy = 1;

	return createSampler(SamplerNames::CLAMP_LINEAR, samplerDesc);
}

wgpu::Sampler WebGPUSamplerFactory::createRepeatLinearSampler()
{
	wgpu::SamplerDescriptor samplerDesc{};
	// Repeat sampler (repeat, linear filtering)
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

	return createSampler(SamplerNames::REPEAT_LINEAR, samplerDesc);
}

} // namespace engine::rendering::webgpu
