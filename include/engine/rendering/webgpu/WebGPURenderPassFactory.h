#pragma once
#include <glm/glm.hpp>
#include <webgpu/webgpu.hpp>
#include "engine/rendering/webgpu/WebGPURenderPassContext.h"
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include "engine/rendering/ClearFlags.h"

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
	explicit WebGPURenderPassFactory(WebGPUContext& context);

	/**
	 * @brief Creates a default render pass buffer using provided color and depth textures.
	 * @param colorTexture Reference to the color WebGPUTexture.
	 * @param depthTexture Reference to the depth WebGPUDepthTexture.
	 * @return Shared pointer to WebGPURenderPassContext with textures, views, and descriptors.
	 */
	std::shared_ptr<WebGPURenderPassContext> createDefault(
		const std::shared_ptr<WebGPUTexture> &colorTexture,
		const std::shared_ptr<WebGPUDepthTexture> &depthTexture
	);

	/**
	 * @brief Creates a render pass for a specific texture with custom clear flags and background color.
	 * @param colorTexture The color texture to render to.
	 * @param depthTexture The depth texture to use.
	 * @param clearFlags Flags indicating which buffers to clear.
	 * @param backgroundColor The background color to clear to.
	 * @return Shared pointer to WebGPURenderPassContext.
	 */
	std::shared_ptr<WebGPURenderPassContext> createForTexture(
		const std::shared_ptr<WebGPUTexture> &colorTexture,
		const std::shared_ptr<WebGPUDepthTexture> &depthTexture,
		engine::rendering::ClearFlags clearFlags,
		const glm::vec4 &backgroundColor
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
			const std::shared_ptr<WebGPUDepthTexture> &depthTexture,
			const wgpu::RenderPassDescriptor &descriptor
		);

  private:
	WebGPUContext &m_context;
};

} // namespace engine::rendering::webgpu
