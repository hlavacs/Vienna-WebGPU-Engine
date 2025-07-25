#pragma once
#include <webgpu/webgpu.hpp>
#include "engine/rendering/Camera.h"

namespace engine::rendering::webgpu
{

	class WebGPUCamera
	{
	public:
		WebGPUCamera(Camera &camera, wgpu::Device device);
		void updateBuffer(wgpu::Queue queue);

		wgpu::Buffer getUniformBuffer() const;
		const glm::mat4 &getViewMatrix() const;
		const glm::mat4 &getProjectionMatrix() const;

	private:
		Camera &m_camera;
		wgpu::Buffer m_uniformBuffer = nullptr;
	};

} // namespace engine::rendering::webgpu