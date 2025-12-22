#include "OrbitCamera.h"
#include "engine/NodeSystem.h"

namespace demo
{

constexpr float PI = 3.14159265358979323846f;

void updateOrbitCamera(OrbitCameraState &state, std::shared_ptr<engine::scene::CameraNode> camera)
{
	// Normalize azimuth to [0, 2π]
	state.azimuth = fmod(state.azimuth, 2.0f * PI);
	if (state.azimuth < 0)
		state.azimuth += 2.0f * PI;

	// Clamp elevation to avoid gimbal lock
	state.elevation = glm::clamp(state.elevation, -PI / 2.0f + 0.01f, PI / 2.0f - 0.01f);

	// Clamp distance
	state.distance = glm::clamp(state.distance, 0.5f, 20.0f);

	// Convert spherical coordinates to Cartesian
	float x = cos(state.elevation) * sin(state.azimuth);
	float y = sin(state.elevation);
	float z = cos(state.elevation) * cos(state.azimuth);

	glm::vec3 position = state.targetPoint + glm::vec3(x, y, z) * state.distance;

	// Update camera position and look-at
	if (camera && camera->getTransform())
	{
		camera->getTransform()->setLocalPosition(position);
		camera->lookAt(state.targetPoint, glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

void updateDragInertia(OrbitCameraState &state, std::shared_ptr<engine::scene::CameraNode> camera, float deltaTime)
{
	if (!state.active && glm::length(state.velocity) > 1e-4f)
	{
		// Apply inertia
		state.azimuth += state.velocity.x * state.sensitivity * deltaTime;
		state.elevation += state.velocity.y * state.sensitivity * deltaTime;

		// Decay velocity
		state.velocity *= state.inertiaDecay;

		// Update camera position
		updateOrbitCamera(state, camera);
	}
	else if (!state.active)
	{
		// Stop completely when velocity is negligible
		state.velocity = glm::vec2(0.0f);
	}
}

OrbitCameraController::OrbitCameraController(OrbitCameraState &state, std::shared_ptr<engine::scene::CameraNode> camera) : m_orbitState(state), m_camera(camera)
{
	updateOrbitCamera(m_orbitState, m_camera);
}

void OrbitCameraController::update(float deltaTime)
{
	auto input = engine()->input();
	if (!input)
		return;

	// Handle mouse drag for camera rotation
	if (input->isMouseButtonPressed(SDL_BUTTON_LEFT))
	{
		if (!m_orbitState.active)
		{
			// Start dragging
			m_orbitState.active = true;
			m_orbitState.startMouse = input->getMousePosition();
			m_orbitState.velocity = glm::vec2(0.0f);
		}
		else
		{
			// Continue dragging
			glm::vec2 delta = input->getMouseDelta();
			m_orbitState.azimuth -= delta.x * 0.005f;
			m_orbitState.elevation += delta.y * 0.005f;
			m_orbitState.velocity = delta * 0.005f;
			updateOrbitCamera(m_orbitState, m_camera);
		}
	}
	else if (m_orbitState.active)
	{
		// Stop dragging
		m_orbitState.active = false;
	}

	// Handle mouse wheel for zoom
	glm::vec2 wheel = input->getMouseWheel();
	if (wheel.y != 0.0f)
	{
		m_orbitState.distance -= wheel.y * m_orbitState.scrollSensitivity;
		updateOrbitCamera(m_orbitState, m_camera);
	}

	// Apply inertia when not dragging
	updateDragInertia(m_orbitState, m_camera, deltaTime);
}

} // namespace demo
