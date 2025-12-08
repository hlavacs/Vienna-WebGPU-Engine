#include "engine/input/InputManager.h"

namespace engine::input
{

void InputManager::pollEvents()
{
	// Key states are updated by processEvent() calls
	// This could be used for per-frame processing if needed
}

bool InputManager::isKeyPressed(SDL_Scancode key) const
{
	auto it = m_keyStates.find(key);
	return it != m_keyStates.end() && it->second;
}

bool InputManager::isMouseButtonPressed(Uint8 button) const
{
	auto it = m_mouseButtonStates.find(button);
	return it != m_mouseButtonStates.end() && it->second;
}

void InputManager::processEvent(const SDL_Event& event)
{
	switch (event.type)
	{
		case SDL_KEYDOWN:
			m_keyStates[event.key.keysym.scancode] = true;
			break;
		case SDL_KEYUP:
			m_keyStates[event.key.keysym.scancode] = false;
			break;
		case SDL_MOUSEBUTTONDOWN:
			m_mouseButtonStates[event.button.button] = true;
			break;
		case SDL_MOUSEBUTTONUP:
			m_mouseButtonStates[event.button.button] = false;
			break;
		case SDL_MOUSEMOTION:
			m_mousePosition = glm::vec2(static_cast<float>(event.motion.x), static_cast<float>(event.motion.y));
			m_mouseDelta = glm::vec2(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
			break;
		case SDL_MOUSEWHEEL:
			m_mouseWheel = glm::vec2(static_cast<float>(event.wheel.x), static_cast<float>(event.wheel.y));
			break;
		default:
			break;
	}
}

void InputManager::endFrame()
{
	// Reset per-frame values
	m_mouseDelta = glm::vec2(0.0f, 0.0f);
	m_mouseWheel = glm::vec2(0.0f, 0.0f);
}

} // namespace engine::input
