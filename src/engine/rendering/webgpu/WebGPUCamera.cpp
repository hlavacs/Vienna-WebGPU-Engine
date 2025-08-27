
#include "engine/rendering/webgpu/WebGPUCamera.h"
#include "engine/rendering/webgpu/WebGPUContext.h"
#include <glm/glm.hpp>

namespace engine::rendering::webgpu
{
WebGPUCamera::WebGPUCamera(WebGPUContext &context, const engine::rendering::Camera::Handle &cameraHandle) :
	WebGPURenderObject<engine::rendering::Camera>(context, cameraHandle, Type::Camera)
{
	wgpu::BufferDescriptor desc{};
	desc.size = sizeof(glm::mat4) * 2;
	desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	desc.mappedAtCreation = false;
	m_uniformBuffer = context.getDevice().createBuffer(desc);
	updateGPUResources();
}

WebGPUCamera::~WebGPUCamera()
{
	if (m_uniformBuffer)
	{
		m_uniformBuffer.destroy();
		m_uniformBuffer.release();
	}
}

void WebGPUCamera::updateGPUResources()
{
	const auto &camera = getCPUObject();
	struct CameraMatrices
	{
		glm::mat4 view;
		glm::mat4 proj;
	};
	CameraMatrices matrices{camera.getViewMatrix(), camera.getProjectionMatrix()};
	m_context.getQueue().writeBuffer(m_uniformBuffer, 0, &matrices, sizeof(CameraMatrices));
}

void WebGPUCamera::render(wgpu::CommandEncoder & /*encoder*/, wgpu::RenderPassEncoder & /*renderPass*/)
{
	// Camera does not issue draw calls directly
}

} // namespace engine::rendering::webgpu
