#pragma once
#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUBindGroupLayoutInfo.h"
#include "engine/rendering/webgpu/WebGPUMaterial.h"
#include <memory>

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUMaterialFactory : public BaseWebGPUFactory<engine::rendering::Material, WebGPUMaterial>
{
  public:
	using BaseWebGPUFactory::BaseWebGPUFactory;

	explicit WebGPUMaterialFactory(WebGPUContext &context);

	/**
	 * @brief Create a WebGPUMaterial from a Material handle.
	 * @param handle Handle to the Material.
	 * @return Shared pointer to WebGPUMaterial.
	 */
	std::shared_ptr<WebGPUMaterial> createFromHandle(const engine::rendering::Material::Handle &handle) override;

	/**
	 * @brief Create a WebGPUMaterial from a Material handle.
	 * @param handle Handle to the Material.
	 * @param options Material options to apply.
	 * @return Shared pointer to WebGPUMaterial.
	 */
	std::shared_ptr<WebGPUMaterial> createFromHandle(
		const engine::rendering::Material::Handle &handle,
		const WebGPUMaterialOptions &options
	);

  private:
	std::shared_ptr<WebGPUBindGroupLayoutInfo> m_bindGroupLayoutInfo = nullptr;
};
} // namespace engine::rendering::webgpu
