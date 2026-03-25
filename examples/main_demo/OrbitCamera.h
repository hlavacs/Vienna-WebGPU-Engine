#pragma once

#include "engine/NodeSystem.h"

#include <memory>
#include <future>

namespace demo
{

/**
 * @brief State for an orbit camera controller
 */
struct OrbitCameraState
{
	bool active = false;
	glm::vec2 startMouse = glm::vec2(0.0f);
	glm::vec2 previousDelta = glm::vec2(0.0f);
	glm::vec2 velocity = glm::vec2(0.0f);

	float azimuth = 0.0f;
	float elevation = 0.3f;
	float distance = 5.0f;

	glm::vec3 targetPoint = glm::vec3(0.0f);

	float sensitivity = 1.0f;
	float scrollSensitivity = 0.5f;
	float inertiaDecay = 0.92f;
};

/**
 * @brief Updates orbit camera position based on spherical coordinates
 */
void updateOrbitCamera(OrbitCameraState &state, std::shared_ptr<engine::scene::nodes::CameraNode> camera);

/**
 * @brief Updates drag inertia for smooth camera motion
 */
void updateDragInertia(OrbitCameraState &state, std::shared_ptr<engine::scene::nodes::CameraNode> camera, float deltaTime);

/**
 * @brief Custom UpdateNode for orbit camera control via mouse input
 */
class OrbitCameraController : public engine::scene::nodes::UpdateNode
{
  public:
	OrbitCameraController(const OrbitCameraState &state, std::shared_ptr<engine::scene::nodes::CameraNode> camera);

	void update(float deltaTime) override;

	std::shared_ptr<engine::scene::nodes::CameraNode> getCamera() const { return m_camera; }
	OrbitCameraState &getOrbitState() { return m_orbitState; }
	const OrbitCameraState &getOrbitState() const { return m_orbitState; }
	void setOrbitState(const OrbitCameraState &state) { m_orbitState = state; }

	void screenShot();

  private:
	OrbitCameraState m_orbitState; // Store by value, not reference
	std::shared_ptr<engine::scene::nodes::CameraNode> m_camera;
	std::future<void> m_saveFuture;
};

} // namespace demo
