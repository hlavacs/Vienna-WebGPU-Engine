#pragma once

#include <filesystem>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;
} // namespace engine::rendering::webgpu

namespace engine::rendering::ibl::internal
{

/**
 * @brief Shared scaffolding for the IBL baker classes (BRDFLut,
 *        PrefilteredEnv, IrradianceMap).
 *
 * Each baker runs a fullscreen-triangle render pass with no vertex buffer,
 * a custom @group(0) bind-group layout, and one color attachment. The
 * boilerplate (load shader, build pipeline layout, build minimal render
 * pipeline) was duplicated across the three. This helper centralises it.
 *
 * **Why we don't go through WebGPUPipelineManager.** PipelineManager
 * caches by PipelineKey and requires a ShaderInfo from the registry. The
 * IBL bake shaders use non-canonical @group(0) bindings — registering
 * them through the engine's ShaderFactory builder would either need a
 * "standalone" opt-out path or force the bind group to @group(4)+. For
 * now we keep these as one-shot pipelines: they bake once at engine init
 * or env-change time, never participate in the per-frame render-graph
 * orchestration, and never hot-reload.
 *
 * Shader module loading still goes through @c WebGPUShaderFactory and
 * texture/sampler/buffer/bind-group creation still goes through the
 * relevant engine factories — only the render-pipeline object is built
 * directly. See @ref createOneShotPipeline for the supported shape.
 */
struct OneShotPipeline
{
	wgpu::PipelineLayout pipelineLayout = nullptr;
	wgpu::RenderPipeline pipeline       = nullptr;
	wgpu::ShaderModule   shaderModule   = nullptr;

	/// Release in the right order (pipeline first, then layout, then
	/// shader module). Safe to call repeatedly.
	void release();
};

/**
 * @brief Build a fullscreen-triangle render pipeline for a one-shot IBL bake.
 *
 * @param context        Engine context (used for the device + factory access).
 * @param shaderPath     Path to the WGSL file. Loaded via the shader factory.
 * @param bindGroupLayouts Bind-group layouts for the pipeline layout (may be
 *                       empty for shaders with no inputs like brdf_lut.wgsl).
 * @param targetFormat   Color attachment format.
 * @param label          Debug label applied to the layout and pipeline.
 *
 * @return Populated @c OneShotPipeline. On any failure, the returned struct's
 *         pipeline is null — caller checks that and bails. The shader module
 *         is included because it owns lifetime tied to the pipeline.
 */
[[nodiscard]] OneShotPipeline createOneShotPipeline(
	webgpu::WebGPUContext               &context,
	const std::filesystem::path         &shaderPath,
	const wgpu::BindGroupLayout         *bindGroupLayouts,
	uint32_t                             bindGroupLayoutCount,
	wgpu::TextureFormat                  targetFormat,
	const char                          *label
);

/**
 * @brief Encode one fullscreen-triangle pass with @p pipeline + @p bindGroup
 *        targeting @p targetView. Used by every IBL baker.
 *
 * @param encoder    Active command encoder. Caller submits.
 * @param targetView Color attachment view (full texture or single mip view).
 * @param pipeline   Render pipeline created by @ref createOneShotPipeline.
 * @param bindGroup  Bind group for @group(0). nullptr is allowed for
 *                   shaders with no inputs (brdf_lut.wgsl).
 * @param label      Label applied to the render pass.
 */
void recordOneShotPass(
	wgpu::CommandEncoder &encoder,
	wgpu::TextureView     targetView,
	wgpu::RenderPipeline  pipeline,
	wgpu::BindGroup       bindGroup,
	const char           *label
);

} // namespace engine::rendering::ibl::internal
