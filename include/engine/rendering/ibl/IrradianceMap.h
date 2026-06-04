#pragma once

#include <cstdint>
#include <memory>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUTexture;
} // namespace engine::rendering::webgpu

namespace engine::rendering::ibl
{

/**
 * @brief Cosine-weighted diffuse irradiance map — the diffuse counterpart to
 *        PrefilteredEnv's GGX specular convolution.
 *
 * Given a source equirect HDR, bakes a low-resolution (default 64×32)
 * RGBA16Float equirect where each texel holds the radiometric integral of
 * the source environment over a Lambertian hemisphere oriented at that
 * texel's direction. At render time the diffuse IBL tap collapses to
 *
 *     irradiance = textureSample(irradianceMap, smp, equirectUv(normal)).rgb;
 *     diffuse    = irradiance * baseColor * (1 - metallic) * ao;
 *
 * no convolution or clamping at the call site. The leading PI / sampleCount
 * in the shader already cancels the diffuse BRDF's 1/PI, so the consumer
 * doesn't need to fold it in either.
 *
 * **Why so small.** A Lambertian hemisphere is extremely low-frequency —
 * 64×32 is enough to capture every directional gradient a viewer will
 * notice. Bigger doesn't help, smaller starts banding. Total memory ~16 KB.
 *
 * **Lifetime model.** Built when the env texture changes — once at engine
 * init from the default env, again when a scene supplies a custom skybox.
 * Bake is ~5-10ms on integrated GPUs; cached by source-texture identity in
 * Renderer.
 */
class IrradianceMap
{
  public:
	/// Output dimensions. Equirect aspect ratio (2:1) preserved.
	static constexpr uint32_t WIDTH  = 64;
	static constexpr uint32_t HEIGHT = 32;

	IrradianceMap() = default;

	/// Bake from @p sourceEquirect into a fresh texture. Safe to call
	/// repeatedly — replaces any previously-baked result. Returns true on
	/// success.
	bool bake(
		webgpu::WebGPUContext &context,
		const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect
	);

	/// The baked irradiance texture. nullptr until bake() succeeds.
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUTexture> getTexture() const { return m_texture; }

  private:
	std::shared_ptr<webgpu::WebGPUTexture> m_texture;
};

} // namespace engine::rendering::ibl
