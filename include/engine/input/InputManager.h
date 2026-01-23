#pragma once
#include <SDL.h>
#include <glm/glm.hpp>
#include <unordered_map>

namespace engine::input
{

class InputManager
{
  public:
	void pollEvents();
	void processEvent(const SDL_Event &event);

	// Keyboard
	bool isKeyPressed(SDL_Scancode key) const;

	// Mouse buttons
	bool isMouseButtonPressed(Uint8 button) const;

	// Mouse position (in window coordinates)
	glm::vec2 getMousePosition() const { return m_mousePosition; }

	// Mouse delta (movement since last frame)
	glm::vec2 getMouseDelta() const { return m_mouseDelta; }

	// Mouse wheel
	glm::vec2 getMouseWheel() const { return m_mouseWheel; }

	// Call this at the end of each frame to reset per-frame values
	void endFrame();

  private:
	std::unordered_map<SDL_Scancode, bool> m_keyStates;
	std::unordered_map<Uint8, bool> m_mouseButtonStates;
	glm::vec2 m_mousePosition{0.0f, 0.0f};
	glm::vec2 m_mouseDelta{0.0f, 0.0f};
	glm::vec2 m_mouseWheel{0.0f, 0.0f};
};

} // namespace engine::input
