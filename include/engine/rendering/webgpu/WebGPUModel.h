#pragma once

#include "engine/rendering/Model.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include "engine/rendering/webgpu/WebGPUMesh.h"
#include "engine/rendering/webgpu/WebGPURenderObject.h"
#include <memory>

namespace engine::rendering::webgpu
{
/**
 * @brief Options for configuring a WebGPUModel.
 */
struct WebGPUModelOptions
{
	// Any model-specific options can go here
};

/**
 * @class WebGPUModel
 * @brief GPU-side model: combines WebGPUMesh and WebGPUMaterial for rendering.
 */
class WebGPUModel : public WebGPURenderObject<engine::rendering::Model>
{
  public:
	/**
	 * @brief Construct a WebGPUModel from a Model handle and GPU resources.
	 * @param context The WebGPU context.
	 * @param modelHandle Handle to the CPU-side Model.
	 * @param mesh The GPU-side mesh.
	 * @param options Optional model options.
	 */
	WebGPUModel(
		WebGPUContext &context,
		const engine::rendering::Model::Handle &modelHandle,
		std::shared_ptr<WebGPUMesh> mesh,
		WebGPUModelOptions options = {}
	);

	/**
	 * @brief Render the model.
	 * @param encoder The command encoder.
	 * @param renderPass The render pass.
	 */
	void render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass);


	void bind(wgpu::RenderPassEncoder &renderPass) const override
	{
		// Models typically don't bind directly; their meshes and materials handle binding.
	}

	/**
	 * @brief Get the GPU-side mesh.
	 * @return Shared pointer to WebGPUMesh.
	 */
	std::shared_ptr<WebGPUMesh> getMesh() const { return m_mesh; }

	/**
	 * @brief Get the model options.
	 * @return The model options.
	 */
	const WebGPUModelOptions &getOptions() const { return m_options; }

  protected:
	/**
	 * @brief Update GPU resources when CPU model changes.
	 */
	void updateGPUResources() override;

  private:
	std::shared_ptr<WebGPUMesh> m_mesh;
	WebGPUModelOptions m_options;
};

} // namespace engine::rendering::webgpu
