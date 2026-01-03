#pragma once

#include <string>
#include <unordered_map>
#include <webgpu/webgpu.hpp>

#include "engine/debug/Loggable.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;

/**
 * @brief Predefined sampler names for common use cases.
 */
namespace SamplerNames
{
inline constexpr const char *DEFAULT = "default";
inline constexpr const char *MIPMAP_LINEAR = "mipmap_linear";
inline constexpr const char *CLAMP_LINEAR = "clamp_linear";
inline constexpr const char *REPEAT_LINEAR = "repeat_linear";
} // namespace SamplerNames

class WebGPUSamplerFactory : public debug::Loggable
{
  public:
	explicit WebGPUSamplerFactory(WebGPUContext &context);

	/**
	 * @brief Get or create a sampler by name.
	 * @param name Sampler name (use SamplerNames constants).
	 * @return WebGPU sampler. Returns default sampler if name is unknown.
	 * @note For known names (DEFAULT, MIPMAP_LINEAR, CLAMP_LINEAR, REPEAT_LINEAR),
	 *       samplers are created lazily on first access and cached.
	 *       For unknown names, a warning is logged and the default sampler is returned.
	 */
	wgpu::Sampler getSampler(const std::string &name);

	/**
	 * @brief Get or create a sampler by name.
	 * @param name Sampler name (use SamplerNames constants).
	 * @param desc Sampler descriptor.
	 * @return WebGPU sampler.
	 * @note If a sampler with the same name already exists, it will be replaced.
	 */
	wgpu::Sampler createSampler(
		const std::string &name,
		const wgpu::SamplerDescriptor &desc
	);

	/**
	 * @brief Register a custom sampler with a name for later retrieval.
	 * @param name The name to associate with the sampler.
	 * @param sampler The sampler to register.
	 */
	void registerSampler(const std::string &name, wgpu::Sampler sampler);

	/**
	 * @brief Get or create default sampler (repeat, linear filtering).
	 * @return WebGPU sampler.
	 */
	wgpu::Sampler getDefaultSampler();

	/**
	 * @brief Get or create sampler for mipmap generation (clamp to edge, linear).
	 * @return WebGPU sampler.
	 */
	wgpu::Sampler getMipmapSampler();

	/**
	 * @brief Get or create clamp sampler (clamp to edge, linear).
	 * @return WebGPU sampler.
	 */
	wgpu::Sampler getClampLinearSampler();

	/**
	 * @brief Get or create repeat sampler (repeat, linear).
	 * @return WebGPU sampler.
	 */
	wgpu::Sampler getRepeatLinearSampler();

	/**
	 * @brief Clear all cached samplers.
	 */
	void cleanup();

  private:
	WebGPUContext &m_context;
	std::unordered_map<std::string, wgpu::Sampler> m_samplerCache;

	wgpu::Sampler createDefaultSampler();
	wgpu::Sampler createMipmapSampler();
	wgpu::Sampler createClampLinearSampler();
	wgpu::Sampler createRepeatLinearSampler();
};
} // namespace engine::rendering::webgpu
