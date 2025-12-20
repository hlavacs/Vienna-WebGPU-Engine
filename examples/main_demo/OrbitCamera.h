#pragma once

#include "engine/scene/CameraNode.h"
#include "engine/scene/entity/UpdateNode.h"
#include <glm/glm.hpp>
#include <memory>

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
void updateOrbitCamera(OrbitCameraState &state, std::shared_ptr<engine::scene::CameraNode> camera);

/**
 * @brief Updates drag inertia for smooth camera motion
 */
void updateDragInertia(OrbitCameraState &state, std::shared_ptr<engine::scene::CameraNode> camera, float deltaTime);

/**
 * @brief Custom UpdateNode for orbit camera control via mouse input
 */
class OrbitCameraController : public engine::scene::entity::UpdateNode
{
  public:
	OrbitCameraController(OrbitCameraState &state, std::shared_ptr<engine::scene::CameraNode> camera);

	void update(float deltaTime) override;

  private:
	OrbitCameraState &m_orbitState;
	std::shared_ptr<engine::scene::CameraNode> m_camera;
};

} // namespace demo
