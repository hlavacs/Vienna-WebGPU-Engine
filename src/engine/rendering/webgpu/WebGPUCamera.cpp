#include "engine/rendering/webgpu/WebGPUCamera.h"

namespace engine::rendering::webgpu
{

WebGPUCamera::WebGPUCamera(Camera &camera, wgpu::Device device) :
	m_camera(camera)
{
	wgpu::BufferDescriptor desc{};
	desc.size = sizeof(glm::mat4) * 2;
	desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	m_uniformBuffer = device.createBuffer(desc);
}

void WebGPUCamera::updateBuffer(wgpu::Queue queue)
{
	struct
	{
		glm::mat4 view;
		glm::mat4 proj;
	} data{m_camera.getViewMatrix(), m_camera.getProjectionMatrix()};
	queue.writeBuffer(m_uniformBuffer, 0, &data, sizeof(data));
}

wgpu::Buffer WebGPUCamera::getUniformBuffer() const { return m_uniformBuffer; }
const glm::mat4 &WebGPUCamera::getViewMatrix() const { return m_camera.getViewMatrix(); }
const glm::mat4 &WebGPUCamera::getProjectionMatrix() const { return m_camera.getProjectionMatrix(); }

} // namespace engine::rendering::webgpu
