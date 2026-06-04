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
 * @brief GGX-convolved environment-map mip chain — the env half of the
 *        split-sum IBL specular approximation.
 *
 * Given a source equirect HDR texture, bakes a destination RGBA16Float
 * texture with `log2(width) + 1` mip levels. Each mip stores the source
 * environment convolved with the GGX kernel at roughness = mipIndex /
 * (mipCount - 1). The IBL specular tap then does:
 *
 *     let envSpec = textureSampleLevel(prefiltered, smp, uv,
 *                                       roughness * maxMip).rgb;
 *
 * which gives a sharp reflection for low roughness, diffuse blur for high
 * roughness, with linear hardware mip filtering smoothing the in-between
 * steps.
 *
 * Pair with the BRDF integration LUT (BRDFLut.h) to reconstruct the full
 * split-sum result via the Fresnel + envBRDF terms.
 *
 * **Lifetime model.** Built when the env texture changes — typically once
 * at engine init from the default env, and again when a scene supplies a
 * custom skybox. Cheap-ish (~50ms for 1024×512 src × 6 mips on integrated
 * GPUs, less on discrete); fast enough to recompute on scene change but
 * not per-frame.
 */
class PrefilteredEnv
{
  public:
	/// Default mip count fits the typical 1024×512 / 2048×1024 equirect
	/// HDRs we ship. Equivalent to roughness step ≈ 1/5 between mips, which
	/// hardware bilinear filtering smooths into a continuous spectrum.
	static constexpr uint32_t MIP_LEVELS = 6;

	PrefilteredEnv() = default;

	/// Bake from @p sourceEquirect into a fresh mip-chain texture. Safe to
	/// call repeatedly — replaces any previously-baked result. Returns true
	/// on success. The source must be a sampleable 2D texture; the bake
	/// reads from it but never modifies it.
	bool bake(webgpu::WebGPUContext &context,
	          const std::shared_ptr<webgpu::WebGPUTexture> &sourceEquirect);

	/// The baked mip-chain texture. nullptr until bake() succeeds.
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUTexture> getTexture() const { return m_texture; }

	/// log2(width) of the baked texture — i.e. the highest mip index that
	/// downstream samples should clamp `roughness * maxMip` to. 0 when no
	/// bake has succeeded yet.
	[[nodiscard]] uint32_t maxMipLevel() const
	{
		return m_texture ? (MIP_LEVELS - 1) : 0;
	}

  private:
	std::shared_ptr<webgpu::WebGPUTexture> m_texture;
};

} // namespace engine::rendering::ibl
