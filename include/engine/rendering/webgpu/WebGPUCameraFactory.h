#pragma once
#include "engine/rendering/Camera.h"
#include "engine/rendering/webgpu/BaseWebGPUFactory.h"
#include "engine/rendering/webgpu/WebGPUCamera.h"
#include <memory>

namespace engine::rendering::webgpu
{
class WebGPUContext;

class WebGPUCameraFactory : public BaseWebGPUFactory<engine::rendering::Camera, WebGPUCamera>
{
  public:
	using BaseWebGPUFactory::BaseWebGPUFactory;

	explicit WebGPUCameraFactory(WebGPUContext &context);

	/**
	 * @brief Create a WebGPUCamera from a Camera handle.
	 * @param handle Handle to the Camera.
	 * @return Shared pointer to WebGPUCamera.
	 */
	std::shared_ptr<WebGPUCamera> createFromHandle(const engine::rendering::Camera::Handle &handle) override;
};
} // namespace engine::rendering::webgpu
