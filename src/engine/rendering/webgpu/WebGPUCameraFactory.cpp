#include "engine/rendering/webgpu/WebGPUCameraFactory.h"
#include "engine/rendering/webgpu/WebGPUCamera.h"
#include "engine/rendering/webgpu/WebGPUContext.h"

namespace engine::rendering::webgpu
{
WebGPUCameraFactory::WebGPUCameraFactory(WebGPUContext &context) :
	BaseWebGPUFactory<engine::rendering::Camera, WebGPUCamera>(context)
{
}

std::shared_ptr<WebGPUCamera> WebGPUCameraFactory::createFromHandle(const engine::rendering::Camera::Handle &handle)
{
	return std::make_shared<WebGPUCamera>(m_context, handle);
}
} // namespace engine::rendering::webgpu
