#pragma once

#include <string>
#include <utility>

#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

/**
 * @brief Thin RAII wrapper around `wgpu::Sampler`.
 *
 * `wgpu::Sampler` is a refcounted handle whose value-copy does NOT bump the
 * underlying refcount — the wgpu C++ bindings hold one reference per
 * outstanding C++ value. Storing a `wgpu::Sampler` by value in a member
 * looks safe but isn't: when the factory's cache releases the only ref it
 * was tracking, the underlying sampler is destroyed and every other value
 * copy becomes a dangling ID. Past symptom: `Sampler[Id(0,1,vk)] does not
 * exist` panic after Clear All, because pass members (`ShadowPass::
 * m_shadowSampler`, `CompositePass::m_sampler`, `DebugPass::m_sampler`)
 * had captured value copies once at init and never bumped the count.
 *
 * `WebGPUSampler` fixes this by owning exactly one wgpu refcount per C++
 * object — bumped in the constructor (if requested), released in the
 * destructor, transferred (not copied) on move. Consumers store it via
 * `std::shared_ptr<WebGPUSampler>`; sharing the same sampler across
 * passes is one wgpu reference per `shared_ptr`, exactly like every
 * other refcounted GPU resource in this engine.
 *
 * Pull samplers from `WebGPUSamplerFactory` (`getDefaultSampler()`,
 * `getMipmapSampler()`, etc.) — the factory wraps the wgpu handle inside
 * `WebGPUSampler` automatically. Construct manually only for one-off
 * samplers that don't belong in the registry.
 *
 * Pass the raw `wgpu::Sampler` to a `wgpu::BindGroupEntry::sampler` field
 * via `->raw()` — the bind group internally references the underlying
 * sampler, so the value-copy passed to wgpu doesn't need its own ref.
 */
class WebGPUSampler
{
  public:
	WebGPUSampler() = default;

	/**
	 * @brief Take ownership of a wgpu sampler handle.
	 * @param sampler  The wgpu handle to wrap.
	 * @param label    Optional debug label.
	 * @param addRef   When true, bumps the wgpu refcount so this wrapper
	 *                 owns its own ref independently of @p sampler's
	 *                 origin. Pass false only when constructing from a
	 *                 freshly-created handle you don't otherwise retain.
	 */
	explicit WebGPUSampler(wgpu::Sampler sampler, std::string label = {}, bool addRef = false)
		: m_sampler(sampler),
		  m_label(std::move(label))
	{
		if (m_sampler && addRef)
		{
			m_sampler.reference();
		}
	}

	~WebGPUSampler()
	{
		if (m_sampler)
		{
			m_sampler.release();
			m_sampler = nullptr;
		}
	}

	// Non-copyable: copying a raw wgpu::Sampler is the bug this class
	// exists to prevent. Share via std::shared_ptr<WebGPUSampler> instead.
	WebGPUSampler(const WebGPUSampler &)            = delete;
	WebGPUSampler &operator=(const WebGPUSampler &) = delete;

	WebGPUSampler(WebGPUSampler &&other) noexcept
		: m_sampler(other.m_sampler),
		  m_label(std::move(other.m_label))
	{
		other.m_sampler = nullptr;
	}

	WebGPUSampler &operator=(WebGPUSampler &&other) noexcept
	{
		if (this != &other)
		{
			if (m_sampler) m_sampler.release();
			m_sampler       = other.m_sampler;
			m_label         = std::move(other.m_label);
			other.m_sampler = nullptr;
		}
		return *this;
	}

	/// Raw wgpu handle. Use only for passing into `wgpu::BindGroupEntry`
	/// or other one-shot consumers — never store the result by value.
	[[nodiscard]] wgpu::Sampler raw() const { return m_sampler; }

	[[nodiscard]] const std::string &label() const { return m_label; }

	[[nodiscard]] bool valid() const { return static_cast<bool>(m_sampler); }
	explicit            operator bool() const { return valid(); }

  private:
	wgpu::Sampler m_sampler = nullptr;
	std::string   m_label;
};

} // namespace engine::rendering::webgpu
