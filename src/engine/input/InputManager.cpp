#include "engine/input/InputManager.h"

namespace engine::input
{

void InputManager::startFrame()
{
	// Save previous position before polling new one
	m_mousePositionPrevious = m_mousePosition;

	// Poll absolute mouse position (SDL3 reports mouse coordinates as float)
	float mouseX, mouseY;
	SDL_GetMouseState(&mouseX, &mouseY);
	m_mousePosition = glm::vec2(mouseX, mouseY);

	// Poll relative mouse delta (once per frame)
	float deltaX, deltaY;
	SDL_GetRelativeMouseState(&deltaX, &deltaY);
	m_mouseDelta = glm::vec2(deltaX, deltaY);
}

bool InputManager::isKey(SDL_Scancode key) const
{
	if (key >= m_keyStates.size())
		return false;
	return m_keyStates[key];
}

bool InputManager::isKeyDown(SDL_Scancode key) const
{
	if (key >= m_keyStates.size())
		return false;
	return m_keyStates[key] && !m_keyStatesPrevious[key];
}

bool InputManager::isKeyUp(SDL_Scancode key) const
{
	if (key >= m_keyStates.size())
		return false;
	return !m_keyStates[key] && m_keyStatesPrevious[key];
}

bool InputManager::isMouse(Uint8 button) const
{
	if (button >= m_mouseButtonStates.size())
		return false;
	return m_mouseButtonStates[button];
}

bool InputManager::isMouseDown(Uint8 button) const
{
	if (button >= m_mouseButtonStates.size())
		return false;
	return m_mouseButtonStates[button] && !m_mouseButtonStatesPrevious[button];
}

bool InputManager::isMouseUp(Uint8 button) const
{
	if (button >= m_mouseButtonStates.size())
		return false;
	return !m_mouseButtonStates[button] && m_mouseButtonStatesPrevious[button];
}

void InputManager::processEvent(const SDL_Event &event)
{
	switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
		m_keyStates[event.key.scancode] = true;
		break;
	case SDL_EVENT_KEY_UP:
		m_keyStates[event.key.scancode] = false;
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		m_mouseButtonStates[event.button.button] = true;
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		m_mouseButtonStates[event.button.button] = false;
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		// Accumulate wheel input (can receive multiple events per frame)
		m_mouseWheel.x += static_cast<float>(event.wheel.x);
		m_mouseWheel.y += static_cast<float>(event.wheel.y);
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
	m_keyStatesPrevious = m_keyStates;
	m_mouseButtonStatesPrevious = m_mouseButtonStates;
}

} // namespace engine::input
