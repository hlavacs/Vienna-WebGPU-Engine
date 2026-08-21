#include "engine/rendering/webgpu/WebGPUSamplerFactory.h"

#include <spdlog/spdlog.h>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{
using namespace wgpu;

WebGPUSamplerFactory::WebGPUSamplerFactory(WebGPUContext &context) :
	m_context(context) {}

WebGPUSamplerFactory::SamplerPtr WebGPUSamplerFactory::getSampler(const std::string &name)
{
	if (name == SamplerNames::DEFAULT
		|| name == SamplerNames::MIPMAP_LINEAR
		|| name == SamplerNames::CLAMP_LINEAR
		|| name == SamplerNames::CLAMP_NEAREST
		|| name == SamplerNames::REPEAT_LINEAR
		|| name == SamplerNames::SHADOW_COMPARISON)
	{
		// Known name: slot's build_fn rebuilds on demand after eviction or
		// soft-clear, so calling getSampler again after Clear All still
		// returns a valid SamplerPtr. We materialise the shared_ptr via
		// Handle::lock() — the consumer holds its own reference, so the
		// underlying wgpu sampler stays alive even after the slot is
		// dropped by the factory.
		return m_cache.getOrCreate(name, [this, name]() { return buildKnown(name); }).lock();
	}

	spdlog::warn("WebGPUSamplerFactory::getSampler: unknown sampler '{}' — falling back to default", name);
	return getSampler(SamplerNames::DEFAULT);
}

WebGPUSamplerFactory::SamplerPtr WebGPUSamplerFactory::createSampler(
	const std::string             &name,
	const wgpu::SamplerDescriptor &desc
)
{
	// Capture desc by value so the build_fn can re-create the sampler after
	// eviction without relying on the caller keeping the descriptor alive.
	wgpu::SamplerDescriptor captured = desc;
	auto                    build    = [this, name, captured]() -> SamplerPtr {
        wgpu::Sampler raw = m_context.getDevice().createSampler(captured);
        if (!raw)
        {
            spdlog::error("WebGPUSamplerFactory::createSampler: device.createSampler failed for '{}'", name);
            return nullptr;
        }
        return std::make_shared<WebGPUSampler>(raw, name, /*addRef=*/false);
	};

	// If a slot already exists for this name, replace its resource in place
	// — existing consumers see the new sampler on next lock(), without
	// invalidating the slot or breaking any shared_ptrs they hold.
	if (m_cache.find(name).valid())
	{
		auto fresh = build();
		if (!fresh) return nullptr;
		m_cache.replace(name, fresh);
		spdlog::warn("WebGPUSamplerFactory: replaced existing sampler '{}'", name);
		return fresh;
	}
	return m_cache.getOrCreate(name, build).lock();
}

WebGPUSamplerFactory::SamplerPtr WebGPUSamplerFactory::buildKnown(const std::string &name)
{
	wgpu::SamplerDescriptor desc;
	if (name == SamplerNames::DEFAULT)                desc = describeDefault();
	else if (name == SamplerNames::MIPMAP_LINEAR)     desc = describeMipmap();
	else if (name == SamplerNames::CLAMP_LINEAR)      desc = describeClampLinear();
	else if (name == SamplerNames::CLAMP_NEAREST)     desc = describeClampNearest();
	else if (name == SamplerNames::REPEAT_LINEAR)     desc = describeRepeatLinear();
	else if (name == SamplerNames::SHADOW_COMPARISON) desc = describeShadowComparison();
	else
	{
		spdlog::error("WebGPUSamplerFactory::buildKnown: '{}' is not a known sampler name", name);
		return nullptr;
	}

	wgpu::Sampler raw = m_context.getDevice().createSampler(desc);
	if (!raw)
	{
		spdlog::error("WebGPUSamplerFactory::buildKnown: device.createSampler failed for '{}'", name);
		return nullptr;
	}
	return std::make_shared<WebGPUSampler>(raw, name, /*addRef=*/false);
}

wgpu::SamplerDescriptor WebGPUSamplerFactory::describeDefault() const
{
	wgpu::SamplerDescriptor desc{};
	desc.addressModeU  = AddressMode::Repeat;
	desc.addressModeV  = AddressMode::Repeat;
	desc.addressModeW  = AddressMode::Repeat;
	desc.magFilter     = FilterMode::Linear;
	desc.minFilter     = FilterMode::Linear;
	desc.mipmapFilter  = MipmapFilterMode::Linear;
	desc.lodMinClamp   = 0.0f;
	desc.lodMaxClamp   = 8.0f;
	desc.compare       = CompareFunction::Undefined;
	desc.maxAnisotropy = 1;
	return desc;
}

wgpu::SamplerDescriptor WebGPUSamplerFactory::describeMipmap() const
{
	wgpu::SamplerDescriptor desc{};
	desc.addressModeU  = AddressMode::ClampToEdge;
	desc.addressModeV  = AddressMode::ClampToEdge;
	desc.addressModeW  = AddressMode::ClampToEdge;
	desc.magFilter     = FilterMode::Linear;
	desc.minFilter     = FilterMode::Linear;
	desc.mipmapFilter  = MipmapFilterMode::Linear;
	desc.lodMinClamp   = 0.0f;
	desc.lodMaxClamp   = 1.0f;
	desc.compare       = CompareFunction::Undefined;
	desc.maxAnisotropy = 1;
	return desc;
}

wgpu::SamplerDescriptor WebGPUSamplerFactory::describeClampLinear() const
{
	wgpu::SamplerDescriptor desc{};
	desc.addressModeU  = AddressMode::ClampToEdge;
	desc.addressModeV  = AddressMode::ClampToEdge;
	desc.addressModeW  = AddressMode::ClampToEdge;
	desc.magFilter     = FilterMode::Linear;
	desc.minFilter     = FilterMode::Linear;
	desc.mipmapFilter  = MipmapFilterMode::Linear;
	desc.lodMinClamp   = 0.0f;
	desc.lodMaxClamp   = 8.0f;
	desc.compare       = CompareFunction::Undefined;
	desc.maxAnisotropy = 1;
	return desc;
}

wgpu::SamplerDescriptor WebGPUSamplerFactory::describeClampNearest() const
{
	wgpu::SamplerDescriptor desc{};
	desc.addressModeU  = AddressMode::ClampToEdge;
	desc.addressModeV  = AddressMode::ClampToEdge;
	desc.addressModeW  = AddressMode::ClampToEdge;
	desc.magFilter     = FilterMode::Nearest;
	desc.minFilter     = FilterMode::Nearest;
	desc.mipmapFilter  = MipmapFilterMode::Nearest;
	desc.lodMinClamp   = 0.0f;
	desc.lodMaxClamp   = 0.0f;
	desc.compare       = CompareFunction::Undefined;
	desc.maxAnisotropy = 1;
	return desc;
}

wgpu::SamplerDescriptor WebGPUSamplerFactory::describeRepeatLinear() const
{
	wgpu::SamplerDescriptor desc{};
	desc.addressModeU  = AddressMode::Repeat;
	desc.addressModeV  = AddressMode::Repeat;
	desc.addressModeW  = AddressMode::Repeat;
	desc.magFilter     = FilterMode::Linear;
	desc.minFilter     = FilterMode::Linear;
	desc.mipmapFilter  = MipmapFilterMode::Linear;
	desc.lodMinClamp   = 0.0f;
	desc.lodMaxClamp   = 8.0f;
	desc.compare       = CompareFunction::Undefined;
	desc.maxAnisotropy = 1;
	return desc;
}

wgpu::SamplerDescriptor WebGPUSamplerFactory::describeShadowComparison() const
{
	wgpu::SamplerDescriptor desc{};
	desc.addressModeU  = AddressMode::ClampToEdge;
	desc.addressModeV  = AddressMode::ClampToEdge;
	desc.addressModeW  = AddressMode::ClampToEdge;
	desc.magFilter     = FilterMode::Linear;
	desc.minFilter     = FilterMode::Linear;
	desc.mipmapFilter  = MipmapFilterMode::Linear;
	desc.compare       = CompareFunction::LessEqual; // Enables depth comparison.
	desc.lodMinClamp   = 0.0f;
	desc.lodMaxClamp   = 8.0f;
	desc.maxAnisotropy = 1;
	return desc;
}

} // namespace engine::rendering::webgpu
