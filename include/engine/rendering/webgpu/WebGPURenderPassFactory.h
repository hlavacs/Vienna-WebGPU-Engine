#pragma once
#include "engine/rendering/ClearFlags.h"
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{
class WebGPUContext;

/**
 * @class WebGPURenderPassFactory
 * @brief Factory for creating WebGPURenderPassContext objects for render passes.
 */
class WebGPURenderPassFactory
{
  public:
	/**
	 * @brief Constructs a new WebGPURenderPassFactory with a given WebGPU context.
	 * @param context Pointer to the WebGPUContext.
	 */
	explicit WebGPURenderPassFactory(WebGPUContext &context);

	/**
	 * @brief Creates a render pass with optional clear flags and background color.
	 * @param colorTexture The color texture to render to.
	 * @param depthTexture The depth texture to use (optional).
	 * @param clearFlags Flags indicating which buffers to clear (default: clear color and depth).
	 * @param backgroundColor The background color to clear to (default: dark gray).
	 * @param colorTextureLayer Specific layer of the color texture to use (-1 for default view).
	 * @param depthTextureLayer Specific layer of the depth texture to use (-1 for default view).
	 * @return Shared pointer to WebGPURenderPassContext.
	 */
	std::shared_ptr<WebGPURenderPassContext> create(
		const std::shared_ptr<WebGPUTexture> &colorTexture,
		const std::shared_ptr<WebGPUTexture> &depthTexture = nullptr,
		engine::rendering::ClearFlags clearFlags = ClearFlags::SolidColor | ClearFlags::Depth,
		const glm::vec4 &backgroundColor = glm::vec4(0.05f, 0.05f, 0.05f, 1.0f),
		int colorTextureLayer = -1,
		int depthTextureLayer = -1
	);

	/**
	 * @brief Creates a depth-only render pass (no color attachment) for shadow maps.
	 * @param depthTextureView The depth texture view to render to (e.g., specific array layer).
	 * @param clearDepth Whether to clear the depth buffer (default true).
	 * @param clearValue The depth clear value (default 1.0).
	 * @return Shared pointer to WebGPURenderPassContext configured for depth-only rendering.
	 */
	std::shared_ptr<WebGPURenderPassContext> createDepthOnly(
		std::shared_ptr<WebGPUTexture> depthTexture,
		int arrayLayer = -1,
		bool clearDepth = true,
		float clearValue = 1.0f
	);

	/**
	 * @brief Creates a fully configurable render pass buffer with custom textures and descriptor.
	 *        Asserts that the number of color textures matches the descriptor's colorAttachmentCount.
	 * @param colorTextures Vector of shared pointers to color textures.
	 * @param depthTexture Shared pointer to depth texture.
	 * @param descriptor Custom RenderPassDescriptor with attachments configured.
	 * @return Shared pointer to WebGPURenderPassContext with all resources and descriptors set.
	 */
	std::shared_ptr<WebGPURenderPassContext> createCustom(
		const std::vector<std::shared_ptr<WebGPUTexture>> &colorTextures,
		const std::shared_ptr<WebGPUTexture> &depthTexture,
		const wgpu::RenderPassDescriptor &descriptor
	);

	/**
	 * @brief Creates a render pass with N color attachments + 1 depth attachment.
	 *
	 * Convenience wrapper for multi-render-target passes (e.g. deferred
	 * G-buffer) where every attachment uses Clear-then-Store with the same
	 * clear color. Builds the attachment descriptors internally so callers
	 * don't have to assemble @c wgpu::RenderPassColorAttachment by hand.
	 *
	 * Use @ref createCustom when you need per-attachment overrides
	 * (different clear values, mix of Load/Clear, stencil aspect, etc.).
	 *
	 * @param label Debug label for the render pass.
	 * @param colorTextures One texture per fragment-shader @location output.
	 *                      Must contain at least one entry.
	 * @param depthTexture  Depth target. Required; pass a depth-only target
	 *                      if you want to disable stencil.
	 * @param clearColor    Clear color applied to every color attachment.
	 * @param depthClear    Clear value for the depth aspect (default 1.0).
	 * @return Shared render pass context ready to be passed to a command encoder.
	 */
	std::shared_ptr<WebGPURenderPassContext> createMultiTarget(
		const char *label,
		const std::vector<std::shared_ptr<WebGPUTexture>> &colorTextures,
		const std::shared_ptr<WebGPUTexture> &depthTexture,
		const glm::vec4 &clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
		float depthClear = 1.0f
	);

  private:
	WebGPUContext &m_context;
};

} // namespace engine::rendering::webgpu
