#pragma once

#include <memory>
#include <vector>

#include "engine/rendering/RenderPass.h"

namespace engine::rendering::webgpu
{
class GBuffer;
class WebGPUBindGroup;
class WebGPUPipeline;
class WebGPUShaderInfo;
} // namespace engine::rendering::webgpu

namespace engine::rendering
{

struct FrameCache;

/**
 * @class GBufferPass
 * @brief Deferred-shading geometry pass.
 *
 * Renders every visible opaque mesh into a four-attachment G-buffer using a
 * single dedicated shader (@c shader::defaults::GBUFFER ). All scene geometry
 * shares one pipeline because the shader is fixed; per-material data is bound
 * through the standard per-material bind group, which uses the same layout as
 * the forward PBR shader and therefore needs no rebuilding.
 *
 * This pass is intentionally @b not derived from @ref MeshPass: forward and
 * deferred geometry have different inputs (no light or environment bind
 * groups), different outputs (4 color targets), and different lifecycle
 * concerns (owns its own G-buffer). Mirroring the MeshPass loop here gives
 * cleaner ownership without leaking shadow / environment plumbing into the
 * deferred path.
 *
 * Resize is delegated to the owned @ref webgpu::GBuffer .
 */
class GBufferPass : public RenderPass
{
public:
	/**
	 * @brief Construct a deferred geometry pass.
	 *
	 * @param context Shared WebGPU context (used for factory access).
	 * @param initialWidth  Initial G-buffer width in pixels.
	 * @param initialHeight Initial G-buffer height in pixels.
	 */
	GBufferPass(
		std::shared_ptr<webgpu::WebGPUContext> context,
		uint32_t initialWidth,
		uint32_t initialHeight
	);

	~GBufferPass() override = default;

	/**
	 * @brief Creates the G-buffer and resolves the registered G-buffer shader.
	 * @return true on success, false if any prerequisite is missing.
	 */
	bool initialize() override;

	/**
	 * @brief Renders all currently visible items into the G-buffer.
	 *
	 * Reads @c frameCache.gpuRenderItems and ignores forward-only data such as
	 * @c lightUniforms and @c shadowUniforms - lighting is computed in the
	 * composition pass.
	 *
	 * @param frameCache Frame-wide cache populated by Renderer earlier in the frame.
	 */
	void render(FrameCache &frameCache) override;

	/**
	 * @brief Drops the cached pipeline so the next frame rebuilds it.
	 *
	 * Called after window resize, after shader hot-reload, or whenever the
	 * Renderer needs to invalidate per-pass GPU state.
	 */
	void cleanup() override;

	/**
	 * @brief Camera identifier for per-frame bind-group lookup in BindGroupBinder.
	 */
	void setCameraId(uint64_t id) { m_cameraId = id; }

	/**
	 * @brief Indices into @c frameCache.gpuRenderItems that survived culling.
	 */
	void setVisibleIndices(std::vector<size_t> indices) { m_visibleIndices = std::move(indices); }

	/**
	 * @brief Resizes the underlying G-buffer attachments.
	 * @return true if the buffer was actually resized.
	 */
	bool resize(uint32_t width, uint32_t height);

	/**
	 * @brief Access the owned G-buffer (composition pass reads its textures).
	 */
	[[nodiscard]] webgpu::GBuffer &getGBuffer() { return *m_gBuffer; }

	/**
	 * @brief Access the owned G-buffer (read-only).
	 */
	[[nodiscard]] const webgpu::GBuffer &getGBuffer() const { return *m_gBuffer; }

private:
	/**
	 * @brief Returns (creates on miss) a pipeline configured for the given cull mode.
	 *
	 * Two pipeline variants are needed in practice: @c CullMode::Back for sealed
	 * opaque meshes and @c CullMode::None for materials flagged
	 * @c MaterialFeature::Flag::DoubleSided (typical of GLTF assets). All other
	 * pipeline parameters - shader, vertex layout, target formats - are shared.
	 *
	 * @param cullMode Cull mode determined from the current material's feature mask.
	 * @return Cached or freshly-built pipeline; nullptr on failure.
	 */
	std::shared_ptr<webgpu::WebGPUPipeline> getPipelineFor(wgpu::CullMode cullMode);

	struct CachedPipeline
	{
		wgpu::CullMode cullMode;
		std::shared_ptr<webgpu::WebGPUPipeline> pipeline;
	};

	std::unique_ptr<webgpu::GBuffer> m_gBuffer;
	std::shared_ptr<webgpu::WebGPUShaderInfo> m_shader;
	std::vector<CachedPipeline> m_pipelineCache;

	uint64_t m_cameraId = 0;
	std::vector<size_t> m_visibleIndices;
};

} // namespace engine::rendering
