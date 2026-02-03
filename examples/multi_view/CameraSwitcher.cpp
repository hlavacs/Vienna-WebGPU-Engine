#include "CameraSwitcher.h"
#include <spdlog/spdlog.h>

namespace demo
{

CameraSwitcher::CameraSwitcher(
	std::shared_ptr<FreeFlyCameraController> freeFlyCameraController,
	std::vector<std::shared_ptr<engine::scene::nodes::CameraNode>>& cameras
) : m_freeFlyCameraController(freeFlyCameraController), m_cameras(cameras)
{
}

void CameraSwitcher::start()
{
	// Cache input manager reference
	m_input = engine()->input();
}

void CameraSwitcher::update(float deltaTime)
{
	if (!m_input)
		return;

	// Check for number key presses (1-4)
	for (int i = 0; i < 4 && i < m_cameras.size(); ++i)
	{
		if (m_input->isKeyDown(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i)))
		{
			if (i != m_activeIndex)
			{
				m_activeIndex = i;
				m_freeFlyCameraController->setCamera(m_cameras[i]);
				spdlog::info("Switched to camera {}", i + 1);
			}
			break;
		}
	}
}

} // namespace demo
