#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include <glm/glm.hpp>

namespace engine::scene::nodes
{
class Node;
class CameraNode;
}

namespace editor
{

/**
 * @brief Mutable state shared between the editor UI, the editor camera, and main.
 *
 * The UI writes the Viewport panel's pixel size and hover state here each frame;
 * the editor camera reads them (before the engine renders) to size its
 * off-screen target and to gate navigation input to when the viewport is active.
 */
struct EditorState
{
	uint64_t editorCameraId = 0;					 ///< id of the editor's camera node
	glm::uvec2 viewportSize{1280, 720};				 ///< desired off-screen render size (from the Viewport panel)
	bool allowCameraInput = false;					 ///< true while the Viewport panel is hovered

	std::weak_ptr<engine::scene::nodes::Node> selected; ///< currently selected node

	/// Scene camera to look through in the viewport. Empty/expired = the free-fly
	/// editor camera. When set, EditorCameraController mirrors this camera's pose
	/// and projection into the editor camera and suspends free-fly navigation.
	std::weak_ptr<engine::scene::nodes::CameraNode> viewCamera;

	std::filesystem::path currentScenePath;			 ///< path of the open scene (empty = unsaved)
	bool sceneDirty = false;						 ///< unsaved changes present
};

} // namespace editor
