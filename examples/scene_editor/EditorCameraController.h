#pragma once

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/NodeSystem.h"

#include "EditorState.h"

namespace engine::scene
{
class Scene;
}

namespace editor
{

/**
 * @brief Free-fly editor camera.
 *
 * Each frame it sizes its camera's off-screen target to the Viewport panel and
 * flags the camera off-screen-only, so the editor shows the rendered result
 * inside a dockable viewport while the window surface carries the UI. Navigation
 * (right mouse to look, WASD to move, Q/E down/up, Shift to sprint) is active
 * only while the Viewport panel is hovered.
 */
class EditorCameraController : public engine::scene::nodes::UpdateNode
{
  public:
	EditorCameraController(std::shared_ptr<engine::scene::nodes::CameraNode> camera, EditorState *state);

	void start() override;
	void update(float deltaTime) override;

  private:
	std::shared_ptr<engine::scene::nodes::CameraNode> m_camera;
	EditorState *m_state = nullptr;
	engine::input::InputManager *m_input = nullptr;
	float m_moveSpeed = 6.0f;
	float m_mouseSensitivity = 0.12f;
	float m_zoomSpeed = 0.8f; // units per scroll-wheel notch

	// Look-through state: remember the free-fly pose + projection while looking
	// through a scene camera, so switching back restores the previous view.
	bool m_viewActive = false;
	bool m_hasSavedPose = false;
	glm::vec3 m_savedPosition{0.0f};
	glm::quat m_savedRotation{1.0f, 0.0f, 0.0f, 0.0f};
	float m_savedFov = 60.0f;
	float m_savedNear = 0.05f;
	float m_savedFar = 500.0f;
	bool m_savedPerspective = true;
	glm::vec4 m_savedBackground{0.0f}; // editor camera's own background, restored on exit
};

/**
 * @brief Create the editor's free-fly camera and its controller, mark both
 * non-serializable (so they never land in saved scenes), add them to @p scene's
 * root as the main camera, and record the camera id in @p state.
 *
 * Used at startup and whenever a scene is created or opened.
 * @return The created editor camera.
 */
std::shared_ptr<engine::scene::nodes::CameraNode> setupEditorCamera(engine::scene::Scene &scene, EditorState &state);

} // namespace editor
