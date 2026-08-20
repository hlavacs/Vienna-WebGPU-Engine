#include "EditorCameraController.h"

#include <algorithm>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include "engine/scene/Scene.h"

namespace editor
{

EditorCameraController::EditorCameraController(
	std::shared_ptr<engine::scene::nodes::CameraNode> camera,
	EditorState *state
) :
	m_camera(camera), m_state(state)
{
}

void EditorCameraController::start()
{
	// engine() can be null when this node is added to a scene that has no engine
	// context yet (e.g. a freshly loaded scene). update() re-fetches the input
	// once the context is available.
	if (auto *context = engine())
		m_input = context->input();
	if (m_camera)
		m_camera->setOffscreenOnly(true);
}

void EditorCameraController::update(float deltaTime)
{
	if (!m_camera)
		return;

	// Match the camera's off-screen texture AND projection aspect to the
	// Viewport panel so the rendered image fills the panel without stretching
	// and the gizmo overlay aligns with it. Runs before the engine renders this
	// frame, so the size is applied this frame.
	if (m_state)
	{
		const uint32_t width = std::max(1u, m_state->viewportSize.x);
		const uint32_t height = std::max(1u, m_state->viewportSize.y);
		m_camera->setRenderSize(glm::uvec2(width, height));
		m_camera->onRenderAreaChanged(width, height);
	}

	auto &transform = m_camera->getTransform();

	// Look-through mode: when the user picks a scene camera in the viewport, mirror
	// its world pose and projection onto the editor camera (the single off-screen
	// viewport camera) and suspend free-fly. The editor camera's own free-fly pose
	// is saved on entry and restored on exit so switching back resumes that view.
	std::shared_ptr<engine::scene::nodes::CameraNode> viewCamera;
	if (m_state)
		viewCamera = m_state->viewCamera.lock();
	const bool viewActive = viewCamera && m_state && viewCamera->getId() != m_state->editorCameraId;

	if (viewActive && !m_viewActive)
	{
		m_savedPosition = transform.getLocalPosition();
		m_savedRotation = transform.getLocalRotation();
		m_savedFov = m_camera->getFov();
		m_savedNear = m_camera->getNear();
		m_savedFar = m_camera->getFar();
		m_savedPerspective = m_camera->isPerspective();
		m_savedBackground = m_camera->getBackgroundColor();
		m_hasSavedPose = true;
	}
	else if (!viewActive && m_viewActive && m_hasSavedPose)
	{
		transform.setLocalPosition(m_savedPosition);
		transform.setLocalRotation(m_savedRotation);
		m_camera->setFov(m_savedFov);
		m_camera->setNearFar(m_savedNear, m_savedFar);
		m_camera->setPerspective(m_savedPerspective);
		m_camera->setBackgroundColor(m_savedBackground);
	}
	m_viewActive = viewActive;

	if (viewActive)
	{
		auto &source = viewCamera->getTransform();
		transform.setWorldPosition(glm::vec3(source.getWorldMatrix()[3]));
		transform.setWorldRotation(source.getRotation());
		m_camera->setFov(viewCamera->getFov());
		m_camera->setNearFar(viewCamera->getNear(), viewCamera->getFar());
		m_camera->setPerspective(viewCamera->isPerspective());
		// Mirror the looked-through camera's background so its clear colour shows
		// (and live-updates) in the viewport, matching what a built game renders.
		m_camera->setBackgroundColor(viewCamera->getBackgroundColor());
		return;
	}

	if (!m_input)
	{
		if (auto *context = engine())
			m_input = context->input();
	}
	if (!m_input || !m_state || !m_state->allowCameraInput)
		return;

	const glm::vec3 forward = transform.forward();
	const glm::vec3 right = transform.right();

	// Fly navigation follows Unity: mouse-look and WASD/QE movement are active only
	// while the right mouse button is held. That leaves Q/W/E/R free to act as
	// editor tool shortcuts when not flying.
	if (m_input->isMouse(SDL_BUTTON_RIGHT))
	{
		const glm::vec2 delta = m_input->getMouseDelta();
		glm::vec3 euler = transform.getLocalEulerAngles();
		euler.x = glm::clamp(euler.x + delta.y * m_mouseSensitivity, -89.0f, 89.0f);
		euler.y += delta.x * m_mouseSensitivity;
		euler.z = 0.0f;
		transform.setLocalEulerAngles(euler);

		glm::vec3 move(0.0f);
		if (m_input->isKey(SDL_SCANCODE_W)) move += forward;
		if (m_input->isKey(SDL_SCANCODE_S)) move -= forward;
		if (m_input->isKey(SDL_SCANCODE_D)) move += right;
		if (m_input->isKey(SDL_SCANCODE_A)) move -= right;
		// Space / E up, Shift / Q down.
		if (m_input->isKey(SDL_SCANCODE_SPACE)) move.y += 1.0f;
		if (m_input->isKey(SDL_SCANCODE_LSHIFT) || m_input->isKey(SDL_SCANCODE_RSHIFT)) move.y -= 1.0f;
		if (m_input->isKey(SDL_SCANCODE_E)) move.y += 1.0f;
		if (m_input->isKey(SDL_SCANCODE_Q)) move.y -= 1.0f;

		if (glm::length(move) > 0.01f)
			transform.translate(glm::normalize(move) * m_moveSpeed * deltaTime, false);
	}

	// Scroll wheel dollies the camera forward/back (zoom), with or without RMB.
	const float wheel = m_input->getMouseWheel().y;
	if (wheel != 0.0f)
		transform.translate(forward * wheel * m_zoomSpeed, false);
}

std::shared_ptr<engine::scene::nodes::CameraNode> setupEditorCamera(engine::scene::Scene &scene, EditorState &state)
{
	auto root = scene.getRoot();

	// The Scene constructor seeds a gameplay camera (the one a built game renders
	// through). Give it a friendly name so it is not shown as an anonymous
	// "Node N" in the hierarchy, then leave it in the scene. The editor camera
	// created below is a separate, non-serializable camera that drives the
	// viewport and is never saved.
	if (auto existing = scene.getMainCamera(); existing && !existing->getName())
		existing->setName("Main Camera");

	auto camera = std::make_shared<engine::scene::nodes::CameraNode>();
	camera->setName("Editor Camera");
	camera->setFov(60.0f);
	camera->setNearFar(0.05f, 500.0f);
	camera->setPerspective(true);
	camera->setBackgroundColor(glm::vec4(0.12f, 0.13f, 0.15f, 1.0f));
	camera->getTransform().setLocalPosition(glm::vec3(0.0f, 2.0f, 6.0f));
	camera->setSerializable(false);
	scene.setMainCamera(camera);
	if (root)
		root->addChild(camera->asNode());
	state.editorCameraId = camera->getId();

	auto controller = std::make_shared<EditorCameraController>(camera, &state);
	controller->setSerializable(false);
	if (root)
		root->addChild(controller->asNode());

	return camera;
}

} // namespace editor
