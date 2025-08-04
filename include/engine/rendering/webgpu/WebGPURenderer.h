#pragma once
/**
 * @file WebGPURenderer.h
 * @brief Central draw manager for rendering WebGPUModel instances.
 */
#include "engine/rendering/webgpu/WebGPUModel.h"
#include <memory>
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

/**
 * @class WebGPURenderer
 * @brief Manages pipelines, bind group layouts, and draw logic.
 */
class WebGPURenderer
{
  public:
	WebGPURenderer();
	~WebGPURenderer();

	/**
	 * @brief Draw a model using the current pipeline.
	 */
	void drawModel(const WebGPUModel &model, wgpu::RenderPassEncoder &pass);

	// ...other methods for pipeline setup, etc...
  private:
	// ...internal pipeline and layout members...
};

} // namespace engine::rendering::webgpu
