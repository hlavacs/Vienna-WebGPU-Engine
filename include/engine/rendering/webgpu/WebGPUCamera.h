#pragma once
#include "engine/rendering/Camera.h"
#include "engine/rendering/webgpu/WebGPURenderObject.h"
#include <webgpu/webgpu.hpp>

namespace engine::rendering::webgpu
{

class WebGPUCamera : public WebGPURenderObject<engine::rendering::Camera>
{
  public:
	/**
	 * @brief Construct a WebGPUCamera from a Camera handle.
	 * @param context The WebGPU context.
	 * @param cameraHandle Handle to the CPU-side Camera.
	 */
	WebGPUCamera(WebGPUContext &context, const engine::rendering::Camera::Handle &cameraHandle);

	/**
	 * @brief Destructor to clean up GPU resources.
	 */
	~WebGPUCamera() override;

	/**
	 * @brief Update GPU resources from CPU Camera.
	 */
	void updateGPUResources() override;

	/**
	 * @brief Set up camera state for rendering (stub).
	 */
	void render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass) override;

	/**
	 * @brief Get the GPU-side uniform buffer for this camera.
	 */
	wgpu::Buffer getUniformBuffer() const { return m_uniformBuffer; }

	/**
	 * @brief Get the view matrix from the CPU camera.
	 */
	const glm::mat4 &getViewMatrix() const { return getCPUObject().getViewMatrix(); }

	/**
	 * @brief Get the projection matrix from the CPU camera.
	 */
	const glm::mat4 &getProjectionMatrix() const { return getCPUObject().getProjectionMatrix(); }

  private:
	wgpu::Buffer m_uniformBuffer = nullptr;
};

} // namespace engine::rendering::webgpu