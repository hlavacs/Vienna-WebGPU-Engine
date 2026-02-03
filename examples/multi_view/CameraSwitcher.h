#pragma once

#include "FreeFlyCamera.h"
#include "engine/NodeSystem.h"
#include <memory>
#include <vector>

namespace demo
{

/**
 * @brief UpdateNode that handles switching between multiple camera controllers
 * Press 1, 2, 3, or 4 to switch active camera
 */
class CameraSwitcher : public engine::scene::nodes::UpdateNode
{
  public:
	CameraSwitcher(
		std::shared_ptr<FreeFlyCameraController> freeFlyCameraController,
		std::vector<std::shared_ptr<engine::scene::nodes::CameraNode>>& cameras
	);

	void start() override;
	void update(float deltaTime) override;

  private:
	std::shared_ptr<FreeFlyCameraController> m_freeFlyCameraController;
	std::vector<std::shared_ptr<engine::scene::nodes::CameraNode>>& m_cameras;
	int m_activeIndex = 0;
	engine::input::InputManager *m_input = nullptr;
};

} // namespace demo
