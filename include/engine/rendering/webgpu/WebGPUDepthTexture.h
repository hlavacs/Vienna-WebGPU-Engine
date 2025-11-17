
#pragma once
#include "engine/rendering/webgpu/WebGPUTexture.h"
#include <memory>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

class WebGPUContext;

/**
 * @class WebGPUDepthTexture
 * @brief Specialized WebGPUTexture for depth buffers, with additional depth-specific methods.
 */
class WebGPUDepthTexture : public WebGPUTexture
{
  public:
	using WebGPUTexture::WebGPUTexture;

	/**
	 * @brief Resizes the depth buffer to the new dimensions if needed.
	 *        Recreates the texture and view if the size or format changes.
	 * @param context The WebGPU context for resource creation.
	 * @param newWidth The new width in pixels.
	 * @param newHeight The new height in pixels.
	 */
	bool resize(WebGPUContext &context, uint32_t newWidth, uint32_t newHeight);
};

} // namespace engine::rendering::webgpu