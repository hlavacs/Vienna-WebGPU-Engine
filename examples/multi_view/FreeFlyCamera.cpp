#include "FreeFlyCamera.h"
#include "engine/NodeSystem.h"

namespace demo
{

FreeFlyCameraController::FreeFlyCameraController(std::shared_ptr<engine::scene::nodes::CameraNode> camera) : m_camera(camera)
{
}

void FreeFlyCameraController::start()
{
	// Cache input manager reference
	m_input = engine()->input();
}

void FreeFlyCameraController::lateUpdate(float deltaTime)
{
	if (!m_input || !m_camera)
	{
		spdlog::warn("FreeFlyCameraController: Missing input manager or camera");
		return;
	}

	auto &transform = m_camera->getTransform();

	// Apply mouse look with left mouse button
	if (m_input->isMouse(SDL_BUTTON_LEFT))
	{
		glm::vec2 mouseDelta = m_input->getMouseDelta();
		// ✅ Correct - accumulate angles, clamp pitch, no roll
		glm::vec3 currentEuler = transform.getLocalEulerAngles();
		float yaw = mouseDelta.x * m_mouseSensitivity;
		float pitch = mouseDelta.y * m_mouseSensitivity;
		currentEuler.x += pitch;
		currentEuler.y += yaw;
		currentEuler.z = 0.0f; // Keep horizon level
		transform.setLocalEulerAngles(currentEuler);
	}

	// Calculate movement direction using Transform helper methods
	glm::vec3 moveDir(0.0f);

	// Get forward and right from Transform (already normalized)
	glm::vec3 forward = transform.forward();
	glm::vec3 right = transform.right();

	// Project onto horizontal plane for ground movement
	forward.y = 0.0f;
	if (glm::length(forward) > 0.01f)
		forward = glm::normalize(forward);
	right.y = 0.0f;
	if (glm::length(right) > 0.01f)
		right = glm::normalize(right);

	// WASD movement
	if (m_input->isKey(SDL_SCANCODE_W))
		moveDir += forward;
	if (m_input->isKey(SDL_SCANCODE_S))
		moveDir -= forward;
	if (m_input->isKey(SDL_SCANCODE_D))
		moveDir += right;
	if (m_input->isKey(SDL_SCANCODE_A))
		moveDir -= right;

	// Vertical movement (Space/LShift)
	if (m_input->isKey(SDL_SCANCODE_SPACE))
		moveDir.y += 1.0f;
	if (m_input->isKey(SDL_SCANCODE_LSHIFT))
		moveDir.y -= 1.0f;

	// Apply movement using Transform's translate method
	if (glm::length(moveDir) > 0.01f)
	{
		moveDir = glm::normalize(moveDir);
		transform.translate(moveDir * m_moveSpeed * deltaTime, false);
	}
}

} // namespace demo
