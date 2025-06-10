#pragma once
#include <memory>
#include "engine/rendering/webgpu/WebGPUModel.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/Model.h"

namespace engine::rendering::webgpu
{
	class WebGPUModelFactory : public BaseWebGPUFactory<engine::rendering::Model, WebGPUModel>
	{
	public:
		using BaseWebGPUFactory::BaseWebGPUFactory;

		explicit WebGPUModelFactory(WebGPUContext &context);
		std::shared_ptr<WebGPUModel> createFrom(const engine::rendering::Model &model) override;
	};
} // namespace engine::rendering::webgpu
