#pragma once
#include "engine/core/Handle.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include <memory>

namespace engine::rendering::webgpu
{
class WebGPUContext;
class WebGPUPipeline;

class WebGPUMaterialFactory : public BaseWebGPUFactory<engine::rendering::Material, WebGPUMaterial>
{
  public:
	using BaseWebGPUFactory::BaseWebGPUFactory;

	explicit WebGPUMaterialFactory(WebGPUContext &context);

  protected:
	/**
	 * @brief Create a WebGPUMaterial from a Material handle with options.
	 * @param handle Handle to the Material.
	 * @param options Material options to apply.
	 * @return Shared pointer to WebGPUMaterial.
	 */
	std::shared_ptr<WebGPUMaterial> createFromHandleUncached(
		const engine::rendering::Material::Handle &handle,
		const WebGPUMaterialOptions &options
	);

	/**
	 * @brief Create a WebGPUMaterial from a Material handle with pipeline.
	 * @param handle Handle to the Material.
	 * @param pipelineHandle Handle to the pipeline to use for rendering.
	 * @param options Material options to apply.
	 * @return Shared pointer to WebGPUMaterial.
	 */
	std::shared_ptr<WebGPUMaterial> createFromHandleUncached(
		const engine::rendering::Material::Handle &handle,
		const engine::core::Handle<WebGPUPipeline> &pipelineHandle,
		const WebGPUMaterialOptions &options = {}
	);

	/**
	 * @brief Create a WebGPUMaterial from a Material handle.
	 * @param handle Handle to the Material.
	 * @return Shared pointer to WebGPUMaterial.
	 */
	std::shared_ptr<WebGPUMaterial> createFromHandleUncached(const engine::rendering::Material::Handle &handle) override;

  private:
	std::shared_ptr<WebGPUBindGroupLayoutInfo> m_bindGroupLayoutInfo = nullptr;
};
} // namespace engine::rendering::webgpu
