#pragma once

#include <memory>
#include <string_view>
#include <utility>

#include <webgpu/webgpu.hpp>

namespace engine::rendering
{

struct FrameCache; // Forward declaration

namespace webgpu
{
class WebGPUContext; // Forward declaration
class WebGPUBindGroup; // Forward declaration
class WebGPUShaderInfo; // Forward declaration
}

/**
 * @brief Base class for all render passes.
 *
 * Provides a common interface for render passes with:
 * - Initialization and cleanup
 * - Rendering with FrameCache (core rendering data)
 * - WebGPU context access
 *
 * Passes should receive additional dependencies (like RenderCollector) via setters,
 * not as render() parameters.
 */
class RenderPass
{
  public:
	virtual ~RenderPass() = default;

	/**
	 * @brief Initialize the render pass.
	 * Creates GPU resources like pipelines, bind groups, textures, etc.
	 * @return true if initialization succeeded, false otherwise.
	 */
	virtual bool initialize() = 0;

	/**
	 * @brief Render using the frame cache.
	 * @param frameCache The frame cache containing all render data for this frame.
	 */
	virtual void render(FrameCache &frameCache) = 0;

	/**
	 * @brief Bind a bind group to the render pass based on shader info using the shader's bind group layout.
	 * @param renderPass The render pass encoder.
	 * @param webgpuShaderInfo The shader info containing information about bind groups.
	 * @param bindgroup The bind group to bind.
	 * @return true if binding succeeded, false otherwise.
	 */
	static bool bind(
		wgpu::RenderPassEncoder renderPass,
		const std::shared_ptr<webgpu::WebGPUShaderInfo> &webgpuShaderInfo,
		const std::shared_ptr<webgpu::WebGPUBindGroup> &bindgroup
	);

	/**
	 * @brief Clean up GPU resources.
	 */
	virtual void cleanup() = 0;

	/**
	 * @brief Stable, short name for this pass — used by the debug UI and the
	 *        render graph for enable/disable toggles.
	 *
	 * Derived classes override to return a constant string literal (e.g.
	 * "GBuffer", "Composition", "Skybox"). The default returns "Pass" so
	 * unnamed passes still show up in the toggle panel.
	 */
	[[nodiscard]] virtual const char *name() const { return "Pass"; }

	/**
	 * @brief Enable / disable this pass.
	 *
	 * When disabled, the Renderer skips calling @ref render() entirely for
	 * the corresponding stage. Useful for A/B-ing the visual contribution
	 * of one pass without recompiling, and for debugging which pass owns a
	 * given artifact. Disabling a pass whose output other passes consume
	 * (e.g. disabling GBuffer while Composition is on) will leave those
	 * downstream passes sampling stale or zero textures — the toggle does
	 * not try to enforce dependency consistency, that's the user's call.
	 */
	void                       setEnabled(bool enabled) { m_enabled = enabled; }
	[[nodiscard]] bool         isEnabled() const { return m_enabled; }

  protected:
	/**
	 * @brief Constructor for derived classes.
	 * @param context Shared WebGPU context for device, queue, and factory access.
	 */
	explicit RenderPass(std::shared_ptr<webgpu::WebGPUContext> context) :
		m_context(std::move(context))
	{
	}

	/**
	 * @brief Fetch a registered shader by name and verify it compiled.
	 *
	 * Centralises the lookup + validity check + error log every pass runs in
	 * initialize(). Returns null on failure so the caller can simply
	 * `if (!getValidatedShader(name)) return false;`.
	 */
	[[nodiscard]] std::shared_ptr<webgpu::WebGPUShaderInfo> getValidatedShader(std::string_view shaderName) const;

	std::shared_ptr<webgpu::WebGPUContext> m_context;
	bool                                   m_enabled = true;
};

} // namespace engine::rendering
