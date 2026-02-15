#pragma once

#include "engine/NodeSystem.h"

#include <memory>

namespace demo
{

/**
 * @brief Custom UpdateNode for free-fly camera control via WASD + mouse
 */
class FreeFlyCameraController : public engine::scene::nodes::UpdateNode
{
  public:
	FreeFlyCameraController(std::shared_ptr<engine::scene::nodes::CameraNode> camera);

	void start() override;
	void lateUpdate(float deltaTime) override;

	std::shared_ptr<engine::scene::nodes::CameraNode> getCamera() const { return m_camera; }
    
	void setCamera(std::shared_ptr<engine::scene::nodes::CameraNode> camera) { m_camera = camera; }

	// Camera control settings
	void setMoveSpeed(float speed) { m_moveSpeed = speed; }
	void setMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }

  private:
	std::shared_ptr<engine::scene::nodes::CameraNode> m_camera;
	engine::input::InputManager *m_input = nullptr;

	// Camera settings
	float m_moveSpeed = 5.0f;
	float m_mouseSensitivity = 0.1f;
};

} // namespace demo
