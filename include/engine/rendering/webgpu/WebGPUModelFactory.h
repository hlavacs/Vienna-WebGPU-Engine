#pragma once
#include "engine/rendering/Model.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUModel.h"
#include <memory>

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUModelFactory : public BaseWebGPUFactory<engine::rendering::Model, WebGPUModel>
{
  public:
	using BaseWebGPUFactory::BaseWebGPUFactory;

	explicit WebGPUModelFactory(WebGPUContext &context);
	
  protected:
	/**
	 * @brief Create a WebGPUModel from a Model handle.
	 * @param handle Handle to the Model.
	 * @param options Optional model options.
	 * @return Shared pointer to WebGPUModel.
	 */
	std::shared_ptr<WebGPUModel> createFromHandleUncached(
		const engine::rendering::Model::Handle &handle,
		const WebGPUModelOptions &options
	);

	/**
	 * @brief Create a WebGPUModel from a Model handle.
	 * @param handle Handle to the Model.
	 * @return Shared pointer to WebGPUModel.
	 */
	std::shared_ptr<WebGPUModel> createFromHandleUncached(
		const engine::rendering::Model::Handle &handle
	) override;
};
} // namespace engine::rendering::webgpu
