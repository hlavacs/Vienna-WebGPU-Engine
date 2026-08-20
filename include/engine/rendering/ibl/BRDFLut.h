#pragma once

#include <memory>
#include <webgpu/webgpu.hpp>

#include "engine/rendering/cache/ResourceSlot.h"

namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUTexture;
class WebGPUPipeline;
} // namespace engine::rendering::webgpu

namespace engine::rendering::ibl
{

/**
 * @brief Bakes the split-sum BRDF integration LUT — a 256×256 RG16Float
 *        texture indexed by (NdotV, roughness) that holds precomputed
 *        Fresnel (scale, bias) terms.
 *
 * One-shot at engine init. The result is owned here and exposed via
 * `getTexture()` for downstream IBL specular code to sample:
 *
 *     let envBRDF = textureSample(brdfLut, sampler, vec2(NdotV, roughness)).rg;
 *     let spec    = envSample * (F0 * envBRDF.x + envBRDF.y);
 *
 * The texture is tiny (256×256×4 bytes ≈ 256 KB) and never invalidated,
 * so it sits at the global level — not per-scene or per-camera. Construct
 * once during Renderer initialization, then forget.
 */
class BRDFLut
{
  public:
	static constexpr uint32_t LUT_SIZE = 256;

	BRDFLut() = default;

	/// Build the LUT: create the texture, load the shader, run the bake.
	/// Returns true on success. Idempotent — calling twice is a no-op after
	/// the first success.
	bool initialize(webgpu::WebGPUContext &context);

	/// The baked LUT texture. nullptr until initialize() succeeds.
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUTexture> getTexture() const { return m_texture; }

  private:
	std::shared_ptr<webgpu::WebGPUTexture> m_texture;
	bool                                   m_initialized = false;
};

} // namespace engine::rendering::ibl
