#pragma once

#include <array>
#include <unordered_map>

#include <SDL.h>
#include <glm/glm.hpp>

namespace engine::input
{

/**
 * @brief Single source of truth for all input state in the engine
 *
 * InputManager uses a deterministic polling architecture where mouse state is polled
 * exactly once per frame via SDL_GetMouseState and SDL_GetRelativeMouseState in startFrame(),
 * not derived from SDL_MOUSEMOTION events. This ensures multiple systems can safely read
 * the same input state without side effects.
 *
 * Frame flow:
 * 1. startFrame() - Poll SDL mouse position and delta once
 * 2. processEvent() - Handle discrete events (keys, mouse buttons, mouse wheel)
 * 3. Game/editor update - Read input state via getters
 * 4. endFrame() - Reset per-frame values (delta, wheel)
 *
 * @note Mouse position and delta are ALWAYS tracked, regardless of SDL relative mouse mode
 * @note Multiple accessors can safely read input state in the same frame
 */
class InputManager
{
  public:
	/**
	 * @brief Poll SDL input state at the start of each frame
	 *
	 * This method must be called once per frame BEFORE processing SDL events.
	 * It polls absolute mouse position via SDL_GetMouseState and relative mouse delta
	 * via SDL_GetRelativeMouseState, ensuring deterministic input state.
	 *
	 * @note This is the ONLY place where SDL mouse state is queried
	 * @see GameEngine::processEvents() for the frame flow
	 */
	void startFrame();

	/**
	 * @brief Process discrete SDL input events
	 *
	 * Handles keyboard state changes (SDL_KEYDOWN, SDL_KEYUP), mouse button state changes
	 * (SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP), and mouse wheel accumulation (SDL_MOUSEWHEEL).
	 *
	 * @param event SDL event to process
	 * @note Does NOT handle  - mouse position/delta are polled in startFrame()
	 * @note Mouse wheel events are accumulated (multiple events per frame are summed)
	 */
	void processEvent(const SDL_Event &event);

	/**
	 * @brief Check if a keyboard key is currently pressed
	 * @param key SDL scancode of the key to check
	 * @return true if the key is pressed, false otherwise
	 */
	bool isKey(SDL_Scancode key) const;

	/**
	 * @brief Check if a keyboard key was pressed down this frame
	 * @param key SDL scancode of the key to check
	 * @return true if the key was pressed down this frame, false otherwise
	 */
	bool isKeyDown(SDL_Scancode key) const;

	/**
	 * @brief Check if a keyboard key was released this frame
	 * @param key SDL scancode of the key to check
	 * @return true if the key was released this frame, false otherwise
	 */
	bool isKeyUp(SDL_Scancode key) const;

	/**
	 * @brief Check if a mouse button is currently pressed
	 * @param button SDL mouse button index (LEFT, RIGHT, etc.)
	 * @return true if the button is pressed, false otherwise
	 */
	bool isMouse(Uint8 button) const;

	/**
	 * @brief Check if a mouse button was pressed down this frame
	 * @param button SDL mouse button index (LEFT, RIGHT, etc.)
	 * @return true if the button was pressed down this frame, false otherwise
	 */
	bool isMouseDown(Uint8 button) const;

	/**
	 * @brief Check if a mouse button was released this frame
	 * @param button SDL mouse button index (LEFT, RIGHT, etc.)
	 * @return true if the button was released this frame, false otherwise
	 */
	bool isMouseUp(Uint8 button) const;

	/**
	 * @brief Get absolute mouse position in window coordinates
	 * @return Mouse position (x, y) where (0, 0) is top-left of the window
	 * @note Polled once per frame in startFrame() via SDL_GetMouseState
	 */
	glm::vec2 getMousePosition() const { return m_mousePosition; }

	/**
	 * @brief Get absolute mouse position from the previous frame
	 * @return Previous frame's mouse position (x, y) in window coordinates
	 * @note Useful for computing manual deltas or detecting large position changes
	 * @note Updated in startFrame() before polling new position
	 */
	glm::vec2 getMousePositionPrevious() const { return m_mousePositionPrevious; }

	/**
	 * @brief Get mouse movement delta for this frame only
	 * @return Mouse delta (dx, dy) representing movement since last frame
	 * @note Polled once per frame in startFrame() via SDL_GetRelativeMouseState
	 * @note Reset to (0, 0) at endFrame()
	 */
	glm::vec2 getMouseDelta() const { return m_mouseDelta; }

	/**
	 * @brief Get mouse wheel movement for this frame
	 * @return Mouse wheel delta (x, y) where y is vertical scroll, x is horizontal scroll
	 * @note Accumulates multiple SDL_MOUSEWHEEL events in the same frame
	 * @note Reset to (0, 0) at endFrame()
	 */
	glm::vec2 getMouseWheel() const { return m_mouseWheel; }

	/**
	 * @brief Reset per-frame input values at the end of each frame
	 *
	 * Resets mouse delta and mouse wheel to (0, 0). Should be called after all game
	 * logic has finished reading input for the current frame.
	 */
	void endFrame();

  private:
	std::array<bool, SDL_NUM_SCANCODES> m_keyStates{false};	 ///< Keyboard key states (scancode -> pressed)
	std::array<bool, 8> m_mouseButtonStates{false};			 ///< Mouse button states (button -> pressed)

	std::array<bool, SDL_NUM_SCANCODES> m_keyStatesPrevious{false};	 ///< Previous frame's keyboard key states (scancode -> pressed)
	std::array<bool, 8> m_mouseButtonStatesPrevious{false};			 ///< Previous frame's mouse button states (button -> pressed)
	glm::vec2 m_mousePosition{0.0f, 0.0f};		   ///< Absolute mouse position (window coordinates)
	glm::vec2 m_mousePositionPrevious{0.0f, 0.0f}; ///< Previous frame's mouse position
	glm::vec2 m_mouseDelta{0.0f, 0.0f};			   ///< Per-frame mouse delta (reset each frame)
	glm::vec2 m_mouseWheel{0.0f, 0.0f};			   ///< Per-frame mouse wheel delta (reset each frame)
};

} // namespace engine::input
